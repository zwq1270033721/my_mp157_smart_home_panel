#ifndef WIFITOOL_H
#define WIFITOOL_H

#include <QObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QMetaType>

#include <atomic>

// ============================================================
//  WifiTool —— WiFi 操作的工作线程对象
//
//  设计要点:
//   • 所有耗时操作（iw / wpa_supplicant / udhcpc）都通过 QProcess 完成，
//     单次扫描最长 25s、连接最长 20s，绝不能放在 GUI 线程里阻塞事件循环
//   • 本类由 WifiDialog 创建后 moveToThread() 到独立 QThread，
//     GUI 只通过队列信号（QueuedConnection）调用它的槽
//   • abort() 只置一个 std::atomic 标志，可在任意线程调用；
//     sh() 每 100ms 检查一次，保证关闭对话框时线程能及时退出
//
//  板子上的工具路径与出厂镜像对应（见 ~/QtDemos/wifimode 与 wifi3）:
//     /usr/sbin/iw、/usr/sbin/wpa_supplicant、/usr/sbin/wpa_cli、
//     /sbin/ifconfig、/sbin/udhcpc、/usr/bin/killall
// ============================================================

// 扫描到的一个热点
struct WifiInfo
{
    QString ssid;
    int     signal = -100;   // dBm，越接近 0 越强
    bool    secure = false;  // 是否加密（WPA/WPA2）
};
Q_DECLARE_METATYPE(WifiInfo)
Q_DECLARE_METATYPE(QList<WifiInfo>)

class WifiTool : public QObject
{
    Q_OBJECT
public:
    explicit WifiTool(QObject *parent = nullptr);

    // 仅置原子标志（线程安全），不会阻塞。正在跑的 QProcess 会尽快被 kill
    void abort();

public slots:
    void scan();
    void connectTo(const QString &ssid, const QString &password);
    void disconnectWifi();
    void queryCurrentState();

signals:
    void statusChanged(const QString &status);          // 给界面显示的过程信息
    void scanFinished(QList<WifiInfo> networks);        // 扫描结束（可能为空）
    void connectFinished(bool ok, const QString &ip);   // 连接结果 + 拿到的 IP
    void disconnectFinished();
    void currentState(const QString &ssid, const QString &ip, int signalDbm);

private:
    // 同步执行一条命令，超时/中止时 kill 并返回空串
    QString sh(const QString &program, const QStringList &args, int timeoutMs = 30000);
    bool    aborted() const { return m_abort.load(); }

    QString currentIp();
    QString currentSsid(int *signalDbm);

    std::atomic<bool> m_abort{false};
};

#endif // WIFITOOL_H
