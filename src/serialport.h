#ifndef SERIALPORT_H
#define SERIALPORT_H
#include <QDialog>
#include <QMenu>
#include <QAction>
#include <QSocketNotifier>

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include  <sys/ioctl.h>
#include <error.h>



class KeyBoarDialog;
namespace Ui {
class SerialPort;
}

class SerialPort : public QDialog
{
    Q_OBJECT
signals:
    void ModeChanged(bool isSerial);
public:
    explicit SerialPort(QWidget *parent = nullptr);
    ~SerialPort();
private slots:
    void on_pushButton_8_clicked();

    void on_sendpushbutton_clicked();

    void on_pushButton_6_clicked();

    void slot_changeUi(bool isSerial);


    void on_modeChanged_clicked();

    void on_tcpip_clicked();

    void on_tcpPort_clicked();

    void on_tcpMode_clicked();

    void on_yes_clicked();

    void on_chooseinput_returnPressed();

    void on_serialPort_clicked();

    void on_baudrate_clicked();

    void on_dataBit_clicked();

    void on_stopBit_clicked();

    void on_parity_clicked();

private:
    Ui::SerialPort *ui;

    int m_fd=-1;

    qint32 m_baud=9600;
    int  m_dataBit=8;
    int m_stopBit=1;
    int m_parity=0;
    QSocketNotifier *m_notify=nullptr;

    bool isSerialMode=true;
    QString m_stargetIp;
    quint16 m_stargetPort;
    enum  InputMode{
        NONE,
        INPUT_IP,
        INPUT_PORT
    };
    InputMode m_inputMode=NONE;

    bool setSerialConfig(int fd,int buaad,int databit,int stopbit,int parity);
};

#endif // SERIALPORT_H
