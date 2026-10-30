#ifndef WIFIDIALOG_H
#define WIFIDIALOG_H

#include <QDialog>
#include <QList>

#include "wifitool.h"

class QThread;
class QListWidgetItem;

namespace Ui {
class WifiDialog;
}

// ============================================================
//  WiFi 设置界面
//
//  线程模型:
//    本对话框（GUI 线程）只负责界面
//    WifiTool（工作线程）负责 iw / wpa_supplicant / udhcpc
//    两边通过队列信号交互，扫描/连接期间界面不会卡死
//
//  流程:
//    打开 → 查询当前状态 → 刷新扫描 → 选热点 → 输密码 → 连接
//    → 关联成功后跑 DHCP → 显示 IP；再点同一个按钮即断开
// ============================================================
class WifiDialog : public QDialog
{
    Q_OBJECT
public:
    explicit WifiDialog(QWidget *parent = nullptr);
    ~WifiDialog() override;

signals:
    // 发给工作线程的请求（队列连接）
    void scanRequested();
    void connectRequested(const QString &ssid, const QString &password);
    void disconnectRequested();
    void stateRequested();

private slots:
    // 界面控件（auto-connect）
    void on_btnBack_clicked();
    void on_refreshButton_clicked();
    void on_connectButton_clicked();
    void on_wifiListWidget_itemClicked(QListWidgetItem *item);

    // 工作线程回调
    void onStatus(const QString &status);
    void onScanFinished(QList<WifiInfo> networks);
    void onConnectFinished(bool ok, const QString &ip);
    void onDisconnected();
    void onCurrentState(const QString &ssid, const QString &ip, int signalDbm);

private:
    static QString signalText(int dbm);
    void setBusy(bool busy);

    Ui::WifiDialog *ui;
    WifiTool       *m_tool   = nullptr;
    QThread        *m_thread = nullptr;

    QString m_selectedSsid;
    bool    m_connected = false;
};

#endif // WIFIDIALOG_H
