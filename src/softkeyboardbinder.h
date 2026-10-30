#ifndef SOFTKEYBOARDBINDER_H
#define SOFTKEYBOARDBINDER_H

#include <QObject>
#include <QPointer>
#include <QList>
#include <QString>

class QWidget;
class QLineEdit;
class QTextEdit;
class QPlainTextEdit;
class KeyBoarDialog;

// ============================================================
//  SoftKeyboardBinder —— 给任意输入框绑定软键盘（一行搞定）
//
//  完整说明见 docs/软键盘使用说明.md
//  （支持的控件、API、踩过的坑、项目里的接入情况都在那里）
//
//  用法（在任意 QWidget / QDialog 的构造函数里）：
//
//      SoftKeyboardBinder *kb = new SoftKeyboardBinder(this);
//      kb->bind(ui->lineEdit);          // QLineEdit
//      kb->bind(ui->textEdit);          // QTextEdit（自动装到 viewport 上）
//      kb->bind(ui->plainTextEdit);     // QPlainTextEdit
//
//      kb->bind({ui->edit1, ui->edit2, ui->edit3});   // 一次绑多个
//
//  或者一步到位（自建自管，最省事）：
//
//      SoftKeyboardBinder::attach(this, ui->lineEdit);
//      SoftKeyboardBinder::attach(this, {ui->a, ui->b});
//
//  已处理好的坑：
//    • QTextEdit/QPlainTextEdit 的事件实际发给 viewport，自动判断
//    • 键盘服务哪个输入框由 binder 记住，点不同输入框自动切换目标
//    • 切换时旧键盘是 deleteLater 延后销毁，用 QPointer + 比对 sender
//      防止它的 destroyed 回调误清新键盘的状态（这个坑会表现为"按键没反应"）
//    • 只保留一个键盘实例，重复点同一个输入框只做 raise，不重复创建
//    • 绑定对象析构时自动解绑
// ============================================================
class SoftKeyboardBinder : public QObject
{
    Q_OBJECT
public:
    // parent 也就是这些输入框的宿主窗口（会作为键盘的父对象）
    explicit SoftKeyboardBinder(QObject *parent);
    ~SoftKeyboardBinder() override;

    // 绑定一个输入框。已绑过则忽略（防止重复点多次重复装过滤器）。
    void bind(QWidget *input);
    // 一次绑定多个
    void bind(const QList<QWidget *> &inputs);
    // 解绑
    void unbind(QWidget *input);

    // 一步到位：自动 new 一个 binder 挂在 owner 下，绑完返回它。
    // 不用自己保存指针，binder 跟随 owner 生命周期。
    static SoftKeyboardBinder *attach(QObject *owner, QWidget *input);
    static SoftKeyboardBinder *attach(QObject *owner, const QList<QWidget *> &inputs);

    // 键盘弹在哪个屏幕位置：默认贴着输入框下方，放不下就翻到上方
    void setFollowInput(bool on) { m_followInput = on; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // 取输入框真正接收鼠标事件的那个子控件（QTextEdit 是 viewport）
    static QWidget *eventTargetOf(QWidget *input);
    // 从被过滤的对象反查回输入框本体
    QWidget *inputOfEventTarget(QObject *watched) const;
    void popupFor(QWidget *input);
    void placeKeyboard(QWidget *input);
    void appendChar(const QString &ch);
    void backspace();

    struct Binding {
        QWidget *input;        // 输入框本体（可能已析构，用 QPointer 判活）
        QWidget *target;       // 真正装过滤器的对象
        QPointer<QWidget> alive;
    };
    QList<Binding> m_bindings;

    QPointer<KeyBoarDialog> m_kbd;   // QPointer: 键盘被 delete 后自动变 null
    QPointer<QWidget>       m_current; // 键盘当前服务的输入框
    bool m_followInput = true;
};

#endif // SOFTKEYBOARDBINDER_H
