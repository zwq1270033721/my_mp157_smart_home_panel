#ifndef BLUETTOTHWORKER_H
#define BLUETTOTHWORKER_H
#include "qobject.h"
#include <QObject>
#include <QThread>
#include <QString>
#include <QStringList>
#include <QElapsedTimer>
#include <atomic>
#include <QList>

struct BluetoothDevice {
    QString name;
    QString address;
};
Q_DECLARE_METATYPE(BluetoothDevice)

class bluettothworker : public QObject
{
    Q_OBJECT
public:
    explicit bluettothworker(QObject *parent = nullptr);
    void requestStop();    
    std::atomic<bool> m_abort{false};

signals:
    void connectedFinished(bool success=false);
    void scanFinished();
    void foundDevice(const QList<BluetoothDevice> &devices);
    void statusChanged(const QString &status);

public slots:
    void connect(const BluetoothDevice &device);
    void scan();
    void start();
    void stop();


private:
    QString sh(const QString cmd,const QStringList &args,int timeout=10000);
    bool sleepInterrupted(int timeout);
    QString m_connectedAddress;





};


#endif // BLUETTOTHWORKER_H
