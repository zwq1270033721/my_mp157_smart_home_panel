#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPoint>
#include <QPropertyAnimation>
#include <QEasingCurve>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    // 重写事件过滤器，用于捕获滑动手势
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    // 11 个设备按钮的点击槽函数，Qt 自动连接
    void on_btnMusic_clicked();
    void on_btnLight_clicked();
    void on_btnCamera_clicked();
    void on_btnBluetooth_clicked();
    void on_btnWifi_clicked();
    void on_btnLock_clicked();
    void on_btnSun_clicked();
    void on_btnWater_clicked();
    void on_btnWeather_clicked();
    void on_btnMoon_clicked();
    void on_btnShield_clicked();
    // page_4 上的同名功能按钮（objectName 不同，功能相同）
    void on_btnCamera2_clicked();
    void on_btnMoon2_clicked();
    void on_btnShield2_clicked();
    void on_btnWeather2_clicked();
    // page_2 上的同名功能按钮
    void on_btnCamera_2_clicked();
    void on_btnMoon_2_clicked();
    void on_btnShield_2_clicked();
    void on_btnWeather_2_clicked();
    // page_3 上的 APP 按钮（电视、QQ、首页）
    void on_btnTv_clicked();
    void on_btnQq_clicked();
    void on_btnHome_clicked();

    void on_pushButton_clicked();

    void on_keyboardialog_clicked();

    void on_galagame_clicked();

private:
    Ui::MainWindow *ui;
    void slideInBottomFrame();
    // 返回当前页面上那个可滑入的底部面板；该页没有则返回 nullptr
    QWidget *currentSlidePanel() const;
    QPropertyAnimation *propertyAnimation;
    QPoint m_pressPos;   // 记录手指/鼠标按下时的位置
    bool   m_pressed;    // 是否处于按下状态
};
#endif // MAINWINDOW_H
