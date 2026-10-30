#include "wifitool.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>
#include <QtMath>

#include <algorithm>

namespace {

// ---- 板子出厂镜像里的工具路径（不同发行版位置略有差异，启动时探测一次）----
QString pickProgram(const QStringList &candidates, const QString &fallback)
{
    for (const QString &path : candidates) {
        if (QFile::exists(path))
            return path;
    }
    return fallback;
}

const QString kIface     = QStringLiteral("wlan0");
const QString kConf      = QStringLiteral("/tmp/wifi.conf");          // 每次连接时重写
const QString kCtrlDir   = QStringLiteral("/var/run/wpa_supplicant"); // 不存在 wpa 起不来
const QString kUdhcpcSh  = QStringLiteral("/etc/udhcpc.d/50default"); // udhcpc 拿到租约后调它配 IP

const QString kIw = pickProgram({QStringLiteral("/usr/sbin/iw"),
                                 QStringLiteral("/sbin/iw"),
                                 QStringLiteral("/usr/bin/iw")},
                                QStringLiteral("/usr/sbin/iw"));

const QString kIfconfig = pickProgram({QStringLiteral("/sbin/ifconfig"),
                                       QStringLiteral("/usr/bin/ifconfig"),
                                       QStringLiteral("/usr/sbin/ifconfig")},
                                      QStringLiteral("/sbin/ifconfig"));

const QString kWpaSupplicant = pickProgram({QStringLiteral("/usr/sbin/wpa_supplicant"),
                                            QStringLiteral("/sbin/wpa_supplicant"),
                                            QStringLiteral("/usr/bin/wpa_supplicant")},
                                           QStringLiteral("/usr/sbin/wpa_supplicant"));

const QString kWpaCli = pickProgram({QStringLiteral("/usr/sbin/wpa_cli"),
                                     QStringLiteral("/sbin/wpa_cli"),
                                     QStringLiteral("/usr/bin/wpa_cli")},
                                    QStringLiteral("/usr/sbin/wpa_cli"));

const QString kUdhcpc = pickProgram({QStringLiteral("/sbin/udhcpc"),
                                     QStringLiteral("/usr/sbin/udhcpc"),
                                     QStringLiteral("/usr/bin/udhcpc")},
                                    QStringLiteral("/sbin/udhcpc"));

const QString kKillall = pickProgram({QStringLiteral("/usr/bin/killall"),
                                      QStringLiteral("/bin/killall")},
                                     QStringLiteral("/usr/bin/killall"));

// iw 输出的 SSID 里，不可打印字符会写成 \xNN，例如 "My\x20WiFi"
QString unescapeIw(const QString &s)
{
    QByteArray bytes;
    bytes.reserve(s.size());
    for (int i = 0; i < s.size(); ++i) {
        if (s.at(i) == QLatin1Char('\\') && i + 3 < s.size()
            && s.at(i + 1) == QLatin1Char('x')) {
            bool ok = false;
            const uint v = s.mid(i + 2, 2).toUInt(&ok, 16);
            if (ok) {
                bytes.append(char(v));
                i += 3;
                continue;
            }
        }
        bytes.append(s.at(i).toLatin1());
    }
    return QString::fromUtf8(bytes);
}

} // namespace

WifiTool::WifiTool(QObject *parent)
    : QObject(parent)
{
}

void WifiTool::abort()
{
    m_abort.store(true);
}

// 同步跑一条外部命令。
// 不用 waitForFinished(timeout) 一把梭，而是每 100ms 轮询一次 —— 这样才能
// 在对话框关闭（abort）时立刻杀掉子进程，而不是傻等 25 秒。
QString WifiTool::sh(const QString &program, const QStringList &args, int timeoutMs)
{
    if (aborted())
        return QString();

    QProcess p;
    p.start(program, args);
    if (!p.waitForStarted(3000))
        return QString();

    QElapsedTimer timer;
    timer.start();
    while (p.state() != QProcess::NotRunning) {
        if (aborted() || timer.elapsed() > timeoutMs) {
            p.kill();
            p.waitForFinished(1000);
            return QString();
        }
        p.waitForFinished(100);
    }

    return QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError());
}

void WifiTool::scan()
{
    if (aborted())
        return;

    m_abort.store(false);
    emit statusChanged(QStringLiteral("正在扫描..."));

    // 网卡不 up 是扫不到热点的
    sh(kIfconfig, {kIface, QStringLiteral("up")}, 5000);

    QString out = sh(kIw, {QStringLiteral("dev"), kIface, QStringLiteral("scan")}, 25000);

    // 部分驱动第一次 scan 会返回 busy，稍等重试一次
    if (out.isEmpty() && !aborted()) {
        QThread::msleep(800);
        out = sh(kIw, {QStringLiteral("dev"), kIface, QStringLiteral("scan")}, 25000);
    }

    if (out.isEmpty()) {
        emit statusChanged(aborted() ? QStringLiteral("已取消")
                                     : QStringLiteral("扫描失败：无线网卡不可用或无权限"));
        emit scanFinished({});
        return;
    }

    // ---- 解析 iw 输出 ----
    // BSS 00:11:22:33:44:55 (on wlan0)
    //     signal: -45.00 dBm
    //     SSID: MyWiFi
    //     capability: ESS Privacy ShortSlotTime (0x0411)
    //     RSN: * Version: 1
    QList<WifiInfo> raw;
    WifiInfo cur;
    bool inBss = false;

    const QStringList lines = out.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (aborted())
            return;

        const QString t = line.trimmed();

        if (t.startsWith(QStringLiteral("BSS "))) {
            if (inBss && !cur.ssid.isEmpty())
                raw.append(cur);
            cur = WifiInfo();
            inBss = true;
            continue;
        }
        if (!inBss)
            continue;

        if (t.startsWith(QStringLiteral("SSID: "))) {
            cur.ssid = unescapeIw(t.mid(6));
        } else if (t.startsWith(QStringLiteral("signal: "))) {
            cur.signal = qRound(t.mid(8).split(QLatin1Char(' ')).first().toDouble());
        } else if (t.startsWith(QStringLiteral("capability:"))) {
            if (t.contains(QStringLiteral("Privacy")))
                cur.secure = true;
        } else if (t.startsWith(QStringLiteral("RSN:")) || t.startsWith(QStringLiteral("WPA:"))) {
            cur.secure = true;
        }
    }
    if (inBss && !cur.ssid.isEmpty())
        raw.append(cur);

    // 同一个 SSID 可能有多台路由器，只保留信号最强的那条
    QMap<QString, WifiInfo> best;
    for (const WifiInfo &n : raw) {
        auto it = best.find(n.ssid);
        if (it == best.end() || n.signal > it->signal)
            best.insert(n.ssid, n);
    }

    QList<WifiInfo> result = best.values();
    std::sort(result.begin(), result.end(),
              [](const WifiInfo &a, const WifiInfo &b) { return a.signal > b.signal; });

    emit scanFinished(result);
    emit statusChanged(result.isEmpty()
                           ? QStringLiteral("未发现热点")
                           : QStringLiteral("发现 %1 个热点").arg(result.size()));
}

void WifiTool::connectTo(const QString &ssid, const QString &password)
{
    if (aborted())
        return;

    m_abort.store(false);

    if (ssid.isEmpty()) {
        emit statusChanged(QStringLiteral("未选择热点"));
        emit connectFinished(false, QString());
        return;
    }

    emit statusChanged(QStringLiteral("正在连接 %1 ...").arg(ssid));

    // 先清场：旧进程不清掉，wpa_supplicant 会因为 ctrl_interface 被占用而起不来
    sh(kKillall, {QStringLiteral("wpa_supplicant")}, 5000);
    sh(kKillall, {QStringLiteral("udhcpc")}, 5000);
    sh(kIfconfig, {kIface, QStringLiteral("0.0.0.0")}, 5000);
    QThread::msleep(500);

    QDir().mkpath(kCtrlDir);   // 这个目录不存在，wpa_supplicant 会直接退出

    QByteArray conf;
    conf += "ctrl_interface=" + kCtrlDir.toUtf8() + "\n";
    conf += "update_config=1\n\n";
    conf += "network={\n";
    conf += "\tssid=\"" + ssid.toUtf8() + "\"\n";
    if (password.isEmpty())
        conf += "\tkey_mgmt=NONE\n";                  // 开放热点
    else
        conf += "\tpsk=\"" + password.toUtf8() + "\"\n";
    conf += "}\n";

    QFile f(kConf);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit statusChanged(QStringLiteral("写入配置失败：%1").arg(f.errorString()));
        emit connectFinished(false, QString());
        return;
    }
    f.write(conf);
    f.close();

    // -B 后台运行 / -i 网卡 / -c 配置文件
    sh(kWpaSupplicant,
       {QStringLiteral("-B"), QStringLiteral("-i"), kIface, QStringLiteral("-c"), kConf},
       8000);

    // 轮询握手状态（最多 ~10s）
    bool associated = false;
    for (int i = 0; i < 20 && !aborted(); ++i) {
        QThread::msleep(500);
        const QString st = sh(kWpaCli,
                              {QStringLiteral("-i"), kIface, QStringLiteral("status")},
                              3000);
        if (st.contains(QStringLiteral("wpa_state=COMPLETED"))) {
            associated = true;
            break;
        }
    }

    if (aborted()) {
        emit connectFinished(false, QString());
        return;
    }
    if (!associated) {
        emit statusChanged(QStringLiteral("连接失败：密码错误或热点不可达"));
        emit connectFinished(false, QString());
        return;
    }

    // 关键一步：wpa_supplicant 只完成"关联"（二层），必须再跑 DHCP 才能拿到 IP
    emit statusChanged(QStringLiteral("已关联，正在获取 IP..."));
    sh(kUdhcpc,
       {QStringLiteral("-i"), kIface,
        QStringLiteral("-n"),           // 拿不到租约就退出
        QStringLiteral("-q"),           // 拿到租约后退出
        QStringLiteral("-t"), QStringLiteral("5"),
        QStringLiteral("-T"), QStringLiteral("3"),
        QStringLiteral("-s"), kUdhcpcSh},
       20000);

    const QString ip = currentIp();
    if (ip.isEmpty()) {
        emit statusChanged(QStringLiteral("已关联，但未获取到 IP"));
        emit connectFinished(false, QString());
        return;
    }

    emit statusChanged(QStringLiteral("连接成功，IP：%1").arg(ip));
    emit connectFinished(true, ip);
}

void WifiTool::disconnectWifi()
{
    emit statusChanged(QStringLiteral("正在断开..."));

    sh(kWpaCli, {QStringLiteral("-i"), kIface, QStringLiteral("disconnect")}, 5000);
    sh(kKillall, {QStringLiteral("udhcpc")}, 5000);
    sh(kKillall, {QStringLiteral("wpa_supplicant")}, 5000);
    sh(kIfconfig, {kIface, QStringLiteral("0.0.0.0")}, 5000);
    QThread::msleep(300);

    emit disconnectFinished();
    emit statusChanged(QStringLiteral("已断开"));
}

void WifiTool::queryCurrentState()
{
    int dbm = -100;
    const QString ssid = currentSsid(&dbm);
    emit currentState(ssid, ssid.isEmpty() ? QString() : currentIp(), dbm);
}

QString WifiTool::currentIp()
{
    const QString out = sh(kIfconfig, {kIface}, 5000);
    // busybox: "inet addr:192.168.1.5" / 新版: "inet 192.168.1.5"
    const QRegularExpression re(
        QStringLiteral("inet (?:addr:)?([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+)"));
    const QRegularExpressionMatch m = re.match(out);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString WifiTool::currentSsid(int *signalDbm)
{
    if (signalDbm)
        *signalDbm = -100;

    const QString out = sh(kIw, {QStringLiteral("dev"), kIface, QStringLiteral("link")}, 5000);
    if (out.contains(QStringLiteral("Not connected")))
        return QString();

    QString ssid;
    const QStringList lines = out.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString t = line.trimmed();
        if (t.startsWith(QStringLiteral("SSID: ")))
            ssid = unescapeIw(t.mid(6));
        else if (signalDbm && t.startsWith(QStringLiteral("signal: ")))
            *signalDbm = qRound(t.mid(8).split(QLatin1Char(' ')).first().toDouble());
    }
    return ssid;
}
