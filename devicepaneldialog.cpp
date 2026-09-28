#include "devicepaneldialog.h"
#include "ui_DevicePanelDialog.h"

DevicePanelDialog::DevicePanelDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DevicePanelDialog)
{
    ui->setupUi(this);
}

DevicePanelDialog::~DevicePanelDialog()
{
    delete ui;
}

// ===== 以下是各设备按钮的点击处理函数，后续按需填充业务逻辑 =====

void DevicePanelDialog::on_btnMusic_clicked()
{
    // 音乐控制
}

void DevicePanelDialog::on_btnLight_clicked()
{
    // 灯光控制
}

void DevicePanelDialog::on_btnCamera_clicked()
{
    // 摄像头
}

void DevicePanelDialog::on_btnBluetooth_clicked()
{
    // 蓝牙
}

void DevicePanelDialog::on_btnWifi_clicked()
{
    // WiFi
}

void DevicePanelDialog::on_btnLock_clicked()
{
    // 门锁
}

void DevicePanelDialog::on_btnSun_clicked()
{
    // 亮度/照明
}

void DevicePanelDialog::on_btnWater_clicked()
{
    // 水阀
}

void DevicePanelDialog::on_btnWeather_clicked()
{
    // 天气
}

void DevicePanelDialog::on_btnMoon_clicked()
{
    // 夜间模式
}

void DevicePanelDialog::on_btnShield_clicked()
{
    // 安防
}
