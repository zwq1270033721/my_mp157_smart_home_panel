#include "bluettothworker.h"
#include "qobject.h"
#include "qstringliteral.h"
#include <QProcess>
#include <QString>
#include <QFile>



bluettothworker::bluettothworker(QObject *parent) : QObject(parent) {}

void bluettothworker::requestStop() {
    m_abort.store(true);
}

bool bluettothworker::sleepInterrupted(int timeout)
{
    QElapsedTimer timer;
    timer.start();
    while(timer.elapsed() < timeout)
    {
        if(m_abort.load())
        {
            return false;
        }
        QThread::msleep(50);
    }
    return true;
}

QString bluettothworker::sh(QString cmd,const QStringList &args,int timeout) {
    if (m_abort.load()) {
        return QString();
    }
    QProcess p;
    p.start(cmd,args);
    if(!p.waitForStarted(3000))
    {
        return QString();
    }
    QElapsedTimer timer;
    timer.start();
    while(p.state() != QProcess::NotRunning)
    {
        if(m_abort.load() || timer.elapsed() >= timeout)
        {
            p.kill();
            p.waitForFinished(3000);
            return QString();
        }
        // 必须调用 waitForFinished 让 QProcess 处理事件，
        // 否则 state() 永远停在 Running，只能空转到超时再 kill
        p.waitForFinished(100);
    }
    return QString::fromUtf8(p.readAllStandardOutput()+p.readAllStandardError());
}

void bluettothworker::connect(const BluetoothDevice &device){
    if(m_abort.load()||device.address.isEmpty())
    {
        return;
    }
    QString result = sh(QString("bluetoothctl"),{QStringLiteral("connect"),device.address});
    if(result.contains("Connection successful"))
    {
        emit connectedFinished(true);
        m_connectedAddress = device.address;
        emit statusChanged(QStringLiteral("连接成功"));
    }
    else
    {
        emit connectedFinished(false);
        emit statusChanged(QStringLiteral("连接失败"));
    }


}


void bluettothworker::scan(){
    if(m_abort.load())
    {
        return;
    }
    sh(QString("bluetoothctl"),{QStringLiteral("scan"),QStringLiteral("on")},15000);
    sleepInterrupted(3000);
    emit scanFinished();
    emit statusChanged(QStringLiteral("扫描完成"));
    QString result = sh(QString("bluetoothctl"),{QStringLiteral("devices")},1000);
    QStringList lines = result.split("\n");
    QList<BluetoothDevice> resultDevices;
    for(const QString &line : lines)
    {
        if(!line.startsWith("Device "))
        {
            continue;
        }
        // Qt 5.14 起枚举从 QString:: 移到 Qt::，Qt 6 已移除旧名。
        // 板子用的是 Qt 5.12.9，主机是 Qt 6.4.2，两个都要能编。
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        const auto splitBehavior = Qt::SkipEmptyParts;
#else
        const auto splitBehavior = QString::SkipEmptyParts;
#endif
        QStringList parts = line.mid(7).split(" ", splitBehavior);
        if(parts.isEmpty())
        {
            continue;
        }
        sh(QString("bluetoothctl"),{QStringLiteral("scan"),QStringLiteral("off")},5000);
        BluetoothDevice device;
        device.address = parts.first();
        device.name=parts.mid(1).join(QLatin1Char(' '));
        resultDevices.append(device);
    }    
    emit statusChanged("找到设备");
    emit foundDevice(resultDevices);
}
void bluettothworker::start(){  
    m_abort.store(false);
    emit statusChanged(QStringLiteral("正在初始化蓝牙…"));
        if (!QFile::exists(QStringLiteral("/sys/class/bluetooth/hci0"))) {
        // systemctl 启动 BlueZ 守护进程 bluetoothd
        sh(QString("systemctl"), {QStringLiteral("start"), QStringLiteral("bluetooth")}, 15000);

        // RTL8723DS 需要先下载固件才会出现 hci0
        sh(QString("rtk_hciattach"),
           {QStringLiteral("-s"), QStringLiteral("115200"),
            QStringLiteral("/dev/ttySTM3"), QStringLiteral("rtk_h5")}, 30000);

        sh(QString("/bin/bash"),
           {QStringLiteral("-c"),
            QStringLiteral("hciconfig hci0 up || (sleep 2; hciconfig hci0 up)")}, 15000);
    }
    emit statusChanged(QStringLiteral("蓝牙已开启"));
    sh(QString("bluetoothctl"), {QStringLiteral("power"), QStringLiteral("on")}, 15000);

}
void bluettothworker::stop(){
    emit statusChanged(QStringLiteral("正在关闭蓝牙…"));
    sh(QString("bluetoothctl"), {QStringLiteral("scan"), QStringLiteral("off")}, 15000);
    sh(QString("bluetoothctl"), {QStringLiteral("power"), QStringLiteral("off")}, 15000);
    if(!m_connectedAddress.isEmpty())
    {
        sh(QString("bluetoothctl"), {QStringLiteral("disconnect"), m_connectedAddress}, 15000);
    }
    else{
        sh(QString("bluetoothctl"), {QStringLiteral("disconnect")}, 15000);
    }
    m_connectedAddress.clear();
    emit connectedFinished(false);
    emit statusChanged(QStringLiteral("蓝牙已关闭"));
}


















