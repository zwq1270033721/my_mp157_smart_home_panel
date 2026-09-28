#ifndef KEYBOARDIALOG_H
#define KEYBOARDIALOG_H

#include <QDialog>
#include <QPoint>

class QMouseEvent;

namespace Ui {
class KeyBoarDialog;
}

class KeyBoarDialog : public QDialog
{
    Q_OBJECT

public:
    explicit KeyBoarDialog(QWidget *parent = nullptr);
    ~KeyBoarDialog();

protected:
    // 拖动窗口：板子是裸 framebuffer (linuxfb)，没有窗口管理器，
    // 普通顶层窗口在板子上完全动不了 —— 必须自己实现拖动。
    // (PC 上"能拖"是任务栏/登录界面的窗口管理器白送的，不是代码在起作用)
    //
    // 只需处理鼠标事件即可: 板子接 tslib，Qt 会把触摸合成为鼠标事件
    // (键盘的按键在板子上能按就是证据)。注意 Qt5 的 QWidget 没有
    // touchEvent 虚函数，别去 override 它。
    void mousePressEvent(QMouseEvent *event)   override;
    void mouseMoveEvent(QMouseEvent *event)    override;
    void mouseReleaseEvent(QMouseEvent *event) override;


signals:
    void textEdit(const QString &ch);     // 每次输入一个字符时发出
    void backspacePressed();              // 退格时发出

private slots:
    void on_btnUpper_clicked();

    void on_btnLower_clicked();

    void on_btnNumUpper_clicked();

    void on_btnNumLower_clicked();

    void on_btnReturn_clicked();

    void on_pushButton_clicked();

private:
    void appendText(const QString &text);   // 追加字符到输入缓冲区
    void onBackspace();                     // 退格：删除最后一个字符
    void updateDisplay();                   // 把缓冲区内容刷新到当前页的输入框

private:
    Ui::KeyBoarDialog *ui;
    QString m_input;    // 输入内容（三个页面共用）
    int Index;

    // 拖动窗口用的状态
    bool   m_dragging = false;    // 是否已越过阈值、正在拖
    QPoint m_pressGlobalPos;      // 按下时鼠标/手指的全局坐标
    QPoint m_pressWinTopLeft;     // 按下时窗口左上角的全局坐标
};

#endif // KEYBOARDIALOG_H
