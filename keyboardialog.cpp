#include "keyboardialog.h"
#include "ui_keyboardialog.h"

#include <QPushButton>
#include <QMouseEvent>

// 桌面 Qt6 与板子 Qt5.12.9 的鼠标 API 不同，这里做一层兼容封装
// (与 mainwindow.cpp 里同样的处理方式):
//   Qt6: QMouseEvent::globalPosition()  → 返回 QPointF
//   Qt5: QMouseEvent::globalPos()       → 返回 QPoint
static QPoint globalPosOf(const QMouseEvent *me)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return me->globalPosition().toPoint();
#else
    return me->globalPos();
#endif
}

// 拖动把手 = 顶部 46px 那条 (输入框 + 两侧留白)。
// 键盘页从 y=40 开始，所以 46 能把输入框整条含进去，又碰不到下面的按键。
static const int kDragHandleHeight = 46;

// 按下后移动超过 5px 才算"拖动"，否则算轻点。
// 板子是电阻屏、手指比鼠标粗，没有这个阈值的话轻点一下窗口就会抖偏。
static const int kDragThreshold = 5;

KeyBoarDialog::KeyBoarDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::KeyBoarDialog)
{
    ui->setupUi(this);
    setWindowTitle(" ");
    setAttribute(Qt::WA_TranslucentBackground);
    Index=1;

    // 让三个页面的背景标签对鼠标事件透明，点击穿透到下面的按钮
    ui->bgLower->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    ui->bgUpper->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    ui->bgSymbol->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    // 把背景标签压到最底层，所有按钮自动浮在上面
    ui->bgLower->lower();
    ui->bgUpper->lower();
    ui->bgSymbol->lower();

    // 输入框也要对鼠标透明，否则它会把顶部那条拖动区的按下事件吃掉 ——
    // QTextBrowser 是 QAbstractScrollArea，事件由内部的 viewport 接收，
    // 而且 QTextControl 会消费掉按下事件做文本选择，不会冒泡到本对话框。
    // (和 serialport.cpp 里"要装在 viewport 上"是同一个坑)
    // 这里是只读的输入预览，不需要选中/滚动，透明掉正好当拖动把手。
    ui->inputDisplay->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    // 统一绑定键盘按键：字母/数字/标点 -> 输入，垃圾桶图标 -> 退格
    const QList<QPushButton *> buttons = findChildren<QPushButton *>();
    for (QPushButton *btn : buttons) {
        const QString name = btn->objectName();

        if (name == "btnBackspaceLower" || name == "btnBackspaceUpper" || name == "btnBackspaceSymbol") {
            // 退格键（垃圾桶图标 / x 图标）
            connect(btn, &QPushButton::clicked, this, &KeyBoarDialog::onBackspace);
            continue;
        }

        if (name == "btnSpaceUpper" || name == "btnSpaceLower") {
            // 底部的长条按键 = 空格键
            connect(btn, &QPushButton::clicked, this, [this] { appendText(" "); });
            continue;
        }

        if (name == "btnSymbol") {
            // !?# -> 切换到符号数字页
            connect(btn, &QPushButton::clicked, this, [this] { ui->kbStacked->setCurrentIndex(2); });
            continue;
        }

        if (name == "btnDeadUpper" || name == "btnDeadSymbol" || name == "btn1b") {
            // "符" 按钮 = 死按键，不响应
            continue;
        }

        if (name == "pushButton") {
            // 退出按钮 = 关闭键盘
            connect(btn, &QPushButton::clicked, this, &QDialog::close);
            continue;
        }

        // 切页功能键（asdf/ASDF/_a123/_A123/Return）由自动连接的槽处理，跳过不输入
        if (name == "btnLower" || name == "btnUpper"
            || name == "btnNumLower" || name == "btnNumUpper" || name == "btnReturn") {
            continue;
        }

        // 其余带文字的按键（字母/数字/标点）= 输入字符，按钮上显示什么就输入什么
        if (!btn->text().isEmpty()) {
            connect(btn, &QPushButton::clicked, this, [this, btn] { appendText(btn->text()); });
        }
    }
}

KeyBoarDialog::~KeyBoarDialog()
{
    delete ui;
}

// 按下点在不在"拖动把手"里 —— 即顶部 46px 那条，且不落在"退出"按钮上
static bool inDragHandle(KeyBoarDialog *dlg, const QPoint &winPos)
{
    if (winPos.y() < 0 || winPos.y() >= kDragHandleHeight) return false;
    // "退出"按钮在右上角，它的点击是"关闭键盘"，不能当拖动
    QWidget *btn = dlg->findChild<QWidget *>("pushButton");
    if (btn && btn->geometry().contains(winPos)) return false;
    return true;
}

void KeyBoarDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && inDragHandle(this, event->pos())) {
        m_pressGlobalPos  = globalPosOf(event);
        m_pressWinTopLeft = frameGeometry().topLeft();
        m_dragging        = false;   // 先别拖，越过阈值才算
        event->accept();
        return;
    }
    QDialog::mousePressEvent(event);
}

void KeyBoarDialog::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) {
        const QPoint d = globalPosOf(event) - m_pressGlobalPos;
        // 阈值保护：手指轻点时的微小抖动不算拖动
        if (!m_dragging && (qAbs(d.x()) > kDragThreshold || qAbs(d.y()) > kDragThreshold))
            m_dragging = true;
        if (m_dragging) {
            move(m_pressWinTopLeft + d);
            event->accept();
            return;
        }
    }
    QDialog::mouseMoveEvent(event);
}

void KeyBoarDialog::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging) {
        m_dragging = false;
        event->accept();
        return;
    }
    QDialog::mouseReleaseEvent(event);
}


void KeyBoarDialog::appendText(const QString &text)
{
    m_input += text;
    updateDisplay();
    emit textEdit(text);      // 只发新输入的那个字符
}

void KeyBoarDialog::onBackspace()
{
    m_input.chop(1);
    updateDisplay();
    emit backspacePressed();  // 通知外部退格
}

void KeyBoarDialog::updateDisplay()
{
    ui->inputDisplay->setText(m_input);
}

void KeyBoarDialog::on_btnUpper_clicked()
{
    ui->kbStacked->setCurrentIndex(0);
}


void KeyBoarDialog::on_btnLower_clicked()
{
    ui->kbStacked->setCurrentIndex(1);
}


void KeyBoarDialog::on_btnNumUpper_clicked()
{
    ui->kbStacked->setCurrentIndex(2);
}


void KeyBoarDialog::on_btnNumLower_clicked()
{
    ui->kbStacked->setCurrentIndex(2);
}


void KeyBoarDialog::on_btnReturn_clicked()
{
    ui->kbStacked->setCurrentIndex(1);
}


void KeyBoarDialog::on_pushButton_clicked()
{
    this->reject();
}

