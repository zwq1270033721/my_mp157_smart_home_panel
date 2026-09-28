#include "softkeyboardbinder.h"
#include "keyboardialog.h"

#include <QWidget>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QAbstractScrollArea>
#include <QMouseEvent>
#include <QEvent>
#include <QApplication>
#include <QScreen>
#include <QGuiApplication>

SoftKeyboardBinder::SoftKeyboardBinder(QObject *parent)
    : QObject(parent)
{
}

SoftKeyboardBinder::~SoftKeyboardBinder() = default;

// QTextEdit / QPlainTextEdit 都继承自 QAbstractScrollArea，
// 鼠标事件实际发给它们的 viewport，过滤器必须装到 viewport 上才收得到。
QWidget *SoftKeyboardBinder::eventTargetOf(QWidget *input)
{
    if (auto *sc = qobject_cast<QAbstractScrollArea *>(input))
        return sc->viewport();
    return input;
}

QWidget *SoftKeyboardBinder::inputOfEventTarget(QObject *watched) const
{
    for (const Binding &b : m_bindings) {
        if (b.target == watched)
            return b.input;
    }
    return nullptr;
}

void SoftKeyboardBinder::bind(QWidget *input)
{
    if (!input)
        return;
    bind(QList<QWidget *>{input});
}

void SoftKeyboardBinder::bind(const QList<QWidget *> &inputs)
{
    for (QWidget *input : inputs) {
        if (!input)
            continue;

        // 去重：同一个输入框重复绑定会让一次点击弹两次
        bool exists = false;
        for (const Binding &b : m_bindings) {
            if (b.input == input) { exists = true; break; }
        }
        if (exists)
            continue;

        QWidget *target = eventTargetOf(input);
        target->installEventFilter(this);

        Binding b;
        b.input  = input;
        b.target = target;
        b.alive  = input;      // QPointer 自动在析构时变 null
        m_bindings.append(b);
    }
}

void SoftKeyboardBinder::unbind(QWidget *input)
{
    for (int i = 0; i < m_bindings.size(); ++i) {
        if (m_bindings[i].input == input) {
            if (m_bindings[i].target)
                m_bindings[i].target->removeEventFilter(this);
            m_bindings.removeAt(i);
            return;
        }
    }
}

SoftKeyboardBinder *SoftKeyboardBinder::attach(QObject *owner, QWidget *input)
{
    return attach(owner, QList<QWidget *>{input});
}

SoftKeyboardBinder *SoftKeyboardBinder::attach(QObject *owner, const QList<QWidget *> &inputs)
{
    if (!owner)
        return nullptr;
    auto *kb = new SoftKeyboardBinder(owner);   // 跟随 owner 生命周期
    kb->bind(inputs);
    return kb;
}

// 贴着输入框放键盘：默认压在下方，下面放不开就翻到上方
void SoftKeyboardBinder::placeKeyboard(QWidget *input)
{
    if (!m_followInput || !m_kbd || !input)
        return;

    const QPoint tl = input->mapToGlobal(QPoint(0, 0));

    // QWidget::screen() 是 Qt6 才有的；板子是 Qt5.12.9 没有这个成员。
    // 用 QGuiApplication::screenAt() 拿到输入框所在屏（Qt5.10+ 可用），
    // 拿不到再退回主屏 —— 这样 Qt5 / Qt6 都能编过。
    QScreen *scr = QGuiApplication::screenAt(tl);
    if (!scr)
        scr = QGuiApplication::primaryScreen();
    if (!scr)
        return;
    const QRect avail = scr->availableGeometry();

    int x = tl.x();
    int y = tl.y() + input->height() + 8;

    // 下方放不下就放到输入框上方
    if (y + m_kbd->height() > avail.bottom())
        y = tl.y() - m_kbd->height() - 8;

    // 别越界
    x = qBound(avail.left(), x, avail.right()  - m_kbd->width());
    y = qBound(avail.top(),  y, avail.bottom() - m_kbd->height());

    m_kbd->move(x, y);
}

void SoftKeyboardBinder::popupFor(QWidget *input)
{
    if (!input)
        return;

    // 键盘已开着且服务的就是这个输入框 -> 只提到最前，不重建
    if (m_kbd && m_current == input) {
        m_kbd->raise();
        m_kbd->activateWindow();
        return;
    }

    // 键盘开着但服务的是别的输入框 -> 关掉重开，避免往错的框里写字
    if (m_kbd) {
        m_kbd->close();     // WA_DeleteOnClose -> deleteLater
        // m_kbd 是 QPointer，这里先不等它销毁，直接继续建新的。
        // 旧键盘真正析构时会走 destroyed 回调，那时用比对 sender 的方式
        // 判断"是不是当前这把"，避免误清新键盘的状态。
    }

    m_current = input;
    m_kbd = new KeyBoarDialog(qobject_cast<QWidget *>(parent()));

    connect(m_kbd, &KeyBoarDialog::textEdit, this,
            [this](const QString &ch){ appendChar(ch); });
    connect(m_kbd, &KeyBoarDialog::backspacePressed, this,
            [this](){ backspace(); });

    // 关键：比对 sender。旧键盘是延后销毁的，等它析构时新键盘已经建好，
    // 不比对就会把新键盘的 m_kbd / m_current 一起清空，表现为"按键没反应"。
    connect(m_kbd, &QObject::destroyed, this, [this](QObject *obj){
        if (m_kbd == obj) {          // QPointer 此时尚未置空，用 obj 比对最稳
            m_current = nullptr;
        }
    });

    m_kbd->setAttribute(Qt::WA_DeleteOnClose);
    placeKeyboard(input);
    m_kbd->show();
}

void SoftKeyboardBinder::appendChar(const QString &ch)
{
    if (!m_current)
        return;

    if (auto *le = qobject_cast<QLineEdit *>(m_current.data())) {
        le->insert(ch);                       // 光标处插入，自动跟随
    } else if (auto *te = qobject_cast<QTextEdit *>(m_current.data())) {
        te->insertPlainText(ch);
    } else if (auto *pe = qobject_cast<QPlainTextEdit *>(m_current.data())) {
        pe->insertPlainText(ch);
    }
}

void SoftKeyboardBinder::backspace()
{
    if (!m_current)
        return;

    if (auto *le = qobject_cast<QLineEdit *>(m_current.data())) {
        le->backspace();                      // 自带：删光标前一个字符
    } else if (auto *te = qobject_cast<QTextEdit *>(m_current.data())) {
        te->textCursor().deletePreviousChar();
    } else if (auto *pe = qobject_cast<QPlainTextEdit *>(m_current.data())) {
        pe->textCursor().deletePreviousChar();
    }
}

bool SoftKeyboardBinder::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::MouseButtonPress)
        return QObject::eventFilter(watched, event);

    // 只处理还活着的绑定（QPointer 在输入框析构后自动变 null）
    QWidget *input = inputOfEventTarget(watched);
    if (!input)
        return QObject::eventFilter(watched, event);

    // 输入框已经析构 -> 顺手清掉这条绑定
    bool alive = false;
    for (const Binding &b : m_bindings) {
        if (b.input == input && !b.alive.isNull()) { alive = true; break; }
    }
    if (!alive)
        return QObject::eventFilter(watched, event);

    popupFor(input);
    return true;    // 吃掉这次点击，光标由输入框自己处理
}
