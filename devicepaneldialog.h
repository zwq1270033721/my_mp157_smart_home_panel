#ifndef DEVICEPANELDIALOG_H
#define DEVICEPANELDIALOG_H

#include <QDialog>

namespace Ui {
class DevicePanelDialog;
}

class DevicePanelDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DevicePanelDialog(QWidget *parent = nullptr);
    ~DevicePanelDialog();

private slots:
    // 各个设备按钮的点击槽函数
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

private:
    Ui::DevicePanelDialog *ui;
};

#endif // DEVICEPANELDIALOG_H
