#ifndef SERIALPORT_H
#define SERIALPORT_H
/* ============================================================
   串口网络调试助手 —— 头文件（声明）
   ------------------------------------------------------------
   本类来自 QtDemos/seria2 工程（那边叫 MainWindow），2026-10-12 整体
   搬进本工程，改成一个从主界面按钮弹出的**模态对话框**（MainWindow
   里 SerialPort dlg(this); dlg.exec();）。

   一个对话框同时支持两种"通信口"：
     • 串口模式：QSerialPort。
       板子出厂 rootfs 自带 libQt5SerialPort.so.5.12.9（已核对出厂镜像），
       交叉编译见 ~/QtDemos/tools/qt-build-arm.sh 里的 serialport 分支
       （头文件 INCS + 链接 LIBS 两处，别只改一处）。
     • TCP 模式：QTcpServer / QTcpSocket。
       服务端一律监听 0.0.0.0，客户端才用 ip 去 connectToHost。

   界面是 serialport.ui：1024x600 固定尺寸、绝对定位、**不挂布局**
   —— 本工程其他对话框也是这个套路（QqDialog.ui / HomeDialog.ui 等）。
   所以控件位置全写死在 .ui 里，代码只管连线，别再加 layout。
   ============================================================ */

#include <QDialog>
#include <QMenu>
#include <QAction>
#include <QString>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSerialPort>

QT_BEGIN_NAMESPACE
namespace Ui { class SerialPort; }
QT_END_NAMESPACE

class SerialPort : public QDialog
{
    Q_OBJECT
public:
    explicit SerialPort(QWidget *parent = nullptr);
    ~SerialPort();

    enum InputMode{
        MODE_NONE,
        MODE_IP,
        MODE_PORT,
        MODE_BAUD
    };

    enum NetMode{
        MODE_SERVER,
        MODE_CLIENT
    };

    enum TCP_SerialPortMode{
        MODE_TCP,
        MODE_SERIALPORT
    };

private slots:
    void onNewClient();
    void onReadData();

    void on_ipButton_clicked();

    void on_tcpPortButton_clicked();

    void on_confirmButton_clicked();

    void on_tcpModeButton_clicked();

    void on_clearReceiveButton_clicked();

    void on_tcpModechoseButton_clicked();

    void closeAll();
    //tcp
    void onClientRead();
    void on_sendButton_clicked();

    void on_modeSwitchButton_clicked();

    void on_connectButton_clicked();

private:
    // 统一的接收显示入口：处理 十六进制显示 / 时间戳 / 显示开关 / 字节计数
    void appendData(const QByteArray &data);
    void setStatus(const QString &status);

    Ui::SerialPort *ui;

    qint64 rxBytes = 0;       // 累计接收字节数

    //tcp
    QTcpServer* server;
    QTcpSocket* localclient;
    QTcpSocket* remoteclient;

    QHostAddress ip;
    quint16 port;

    InputMode m_mode;
    NetMode netMode;          // 成员名与枚举类型名区分开，避免遮蔽
    TCP_SerialPortMode tcp_serialport;

    //serialport
    QSerialPort *serial;

    QMenu *portMenu;      // 串口端口选择菜单（点 portButton 时现场枚举, 支持热插拔）
    QString portName;     // 当前选中的端口路径, 如 /dev/ttyUSB2

    int baudSel;
    int dataSel;
    int stopSel;
    int paritySel;

    QMenu *baudRateMenu;
    QAction *baudRate_9600;
    QAction *baudRate_115200;
    QAction *baudRate_230400;
    QAction *baudRate_4800;

    QMenu *dataBitMenu;
    QAction *dataBit_6;
    QAction *dataBit_7;
    QAction *dataBit_8;

    QMenu *stopBitMenu;
    QAction *stopBit_1;
    QAction *stopBit_2;

    QMenu *parityBitMenu;
    QAction *parityBit_none;
    QAction *parityBit_odd;
    QAction *parityBit_even;
};

#endif // SERIALPORT_H
