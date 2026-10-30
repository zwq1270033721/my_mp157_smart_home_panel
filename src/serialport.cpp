#include "serialport.h"
#include "ui_serialport.h"
#include <QCheckBox>
#include <QDebug>
#include <QSerialPortInfo>
#include <QTextCursor>
#include <QTextEdit>
#include <QTime>
#include "softkeyboardbinder.h"  // 板子无实体键盘, 发送框/IP框挂软键盘

// ★本次改动: 对端默认地址。同一份源码要编到两种机器(板子=ARM / 电脑=x86),
//   而"服务端监听"要绑自己、"客户端连接"要连对方 —— 对端地址两台机器上正好相反,
//   一个写死的值不可能同时满足两边, 所以按平台给不同默认值。
//   (服务端统一监听 0.0.0.0, 不受此值影响; ip 只用于客户端 connectToHost)
#ifdef __arm__
static const char *kPeerIp="192.168.7.211";   // 板子: 对端是电脑
#else
static const char *kPeerIp="192.168.7.1";     // 电脑: 对端是板子
#endif

SerialPort::SerialPort(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SerialPort)
{
    //tcp
    ui->setupUi(this);

    // 显示开关默认值：.ui 里这几个复选框默认全未勾选，
    // 若直接按勾选状态决定是否显示，接收区会一片空白。
    ui->showReceiveCheckBox->setChecked(true);
    ui->textWrapCheckBox->setChecked(true);
    ui->receiveTextBrowser->setLineWrapMode(QTextEdit::WidgetWidth);

    // 文本自动换行开关
    connect(ui->textWrapCheckBox, &QCheckBox::toggled, this, [this](bool on) {
        ui->receiveTextBrowser->setLineWrapMode(on ? QTextEdit::WidgetWidth
                                                   : QTextEdit::NoWrap);
    });

    setStatus(QStringLiteral("就绪"));

    // ★本次改动(新增): 本工程里 SerialPort 是模态对话框(dlg.exec()), 而 seria2 那边是主窗口。
    //   主窗口自带标题栏关闭按钮, 对话框没有 —— 必须把 .ui 里的「返回」接到 reject(),
    //   否则点开就关不掉了。
    connect(ui->backButton,&QPushButton::clicked,this,&QDialog::reject);

    // ★本次改动(新增): 板子上没有实体键盘, 发送框和 IP/端口输入框都要能点出软键盘。
    //   实现见 softkeyboardbinder.h(自动处理 QTextEdit 打在 viewport 上等坑)。
    SoftKeyboardBinder::attach(this, {ui->sendTextEdit, ui->configLineEdit});
    server=new QTcpServer(this);
    localclient=new QTcpSocket(this);
    // ✅ 已修复: onNewClient() 里已加 `if(localclient) localclient->deleteLater();` —— 旧 socket 不再泄漏。
    //   (仍只支持单个客户端: 第二个连接会顶掉第一个, 多客户端未做。)
    remoteclient=new QTcpSocket(this);
    connect(server,&QTcpServer::newConnection,this,&SerialPort::onNewClient);

    connect(remoteclient,&QTcpSocket::readyRead,this,&SerialPort::onClientRead);

    //connecedState
    connect(remoteclient,&QTcpSocket::connected,this,[=]{
        ui->receiveTextBrowser->append(QStringLiteral("客户端连接成功"));
        ui->tcpModeButton->setText(QStringLiteral("取消连接"));
        setStatus(QStringLiteral("TCP 客户端已连接"));
    });

    connect(remoteclient,&QTcpSocket::disconnected,this,[=]{
        ui->receiveTextBrowser->append(QStringLiteral("连接断开"));
        ui->tcpModeButton->setText(QStringLiteral("连接"));
        setStatus(QStringLiteral("TCP 连接断开"));
        // 不能在这里 deleteLater()：remoteclient 是成员指针，之后还要用它 connectToHost()，
        // 删掉会变成悬空指针（切模式时 disconnectFromHost() 直接崩）。
    });

    // ★本次改动: 原句只有 connect(...,&QTcpSocket::errorOccurred,...) 一句,
    //   errorOccurred 是 Qt5.15+ 才有的信号, 板子(Qt5.12.9)编不过, 故加版本判断
    connect(remoteclient,
#if QT_VERSION >= QT_VERSION_CHECK(5,15,0)
            &QTcpSocket::errorOccurred,
#else
            QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
#endif
            this,[=]{
        ui->receiveTextBrowser->append(QStringLiteral("错误：")+remoteclient->errorString());
        ui->tcpModeButton->setText(QStringLiteral("连接"));
        setStatus(QStringLiteral("TCP 错误：")+remoteclient->errorString());
    });



    ip=QHostAddress(kPeerIp);   // ★本次改动: 取对端默认地址(见文件顶部 kPeerIp)
    port=8888;
    m_mode=MODE_NONE;
    netMode=MODE_SERVER;
    ui->tcpModeButton->setText("监听");
    ui->tcpModechoseButton->setText("服务端模式");


    //serialport
    serial=new QSerialPort(this);
    // ✅ 已修复：第 41 行已写成 new QSerialPort(this)，串口对象随 SerialPort 一起析构，不再泄漏。
    // ★本次改动: 原为 ui->receiveTextBrowser->append(serial->readAll());
    //   append() 把"每一批 readyRead"都当成一个新段落 —— 串口把同一条数据分两批送到,
    //   就会被拆成两行(实测: PC_TEST_ABCDEFGH 被断成 PC_TEST_ / ABCDEFGH)。
    //   改成"移到末尾续写": 分片自然拼回同一行, 只有发送方真的发了换行才换行。
    // 串口接收统一走 appendData()：它负责 十六进制显示 / 时间戳 / 接收开关 / 字节计数。
    // 仍然"移到末尾续写"而不是 append()，否则一条数据被拆成两批 readyRead 时会断成两行
    // （实测 PC_TEST_ABCDEFGH 被断成 PC_TEST_ / ABCDEFGH）。
    connect(serial,&QSerialPort::readyRead,this,[=](){
        appendData(serial->readAll());
    });
    baudSel=115200;
    dataSel=8;
    stopSel=1;
    paritySel=0;

    tcp_serialport=MODE_TCP;
    // ★本次改动(新增): 串口端口选择菜单 —— 原来端口是写死的字符串
    //   点 portButton 时现场枚举(支持热插拔)。同一份代码 PC/板子通用:
    //   PC 上列出 /dev/ttyUSBx, 板子上列出 /dev/ttySTMx
    portMenu=new QMenu(this);
    ui->portButton->setMenu(portMenu);
    connect(portMenu,&QMenu::aboutToShow,this,[=]{
        portMenu->clear();
        const QList<QSerialPortInfo> ports=QSerialPortInfo::availablePorts();
        if(ports.isEmpty()){
            portMenu->addAction("(未发现串口)")->setEnabled(false);
            return;
        }
        for(const QSerialPortInfo &info:ports){
            QString label=info.portName();
            if(!info.description().isEmpty())
                label+="  —  "+info.description();
            QAction *act=portMenu->addAction(label);
            const QString loc=info.systemLocation();
            connect(act,&QAction::triggered,this,[=]{
                if(serial->isOpen()){
                    ui->receiveTextBrowser->append("请先关闭连接再改参数");
                    return;
                }
                portName=loc;
                ui->portButton->setText(info.portName());
            });
        }
    });
    // 启动先取第一个可用口当默认, 省得没选就点连接
    const QList<QSerialPortInfo> initPorts=QSerialPortInfo::availablePorts();
    if(!initPorts.isEmpty()){
        portName=initPorts.first().systemLocation();
        ui->portButton->setText(initPorts.first().portName());
    }
    // 显示区控件已全部接入：十六进制 / 时间戳 / 接收开关 / 自动换行 / 字节计数 / 状态栏
    // （backButton 在构造函数开头已接到 reject()）。

    baudRateMenu=new QMenu(this);
    baudRate_4800=new QAction("4800",this);
    // ✅ 已修复: 已改成 QAction("4800", this)。
    baudRate_9600=new QAction("9600",this);
    baudRate_115200=new QAction("115200",this);
    baudRate_230400=new QAction("230400",this);
    baudRateMenu->addAction(baudRate_4800);
    baudRateMenu->addAction(baudRate_9600);
    baudRateMenu->addAction(baudRate_115200);
    baudRateMenu->addAction(baudRate_230400);
    ui->baudRateButton->setMenu(baudRateMenu);

    connect(baudRate_4800,&QAction::triggered,this,[=](){
        if(serial->isOpen()){
            ui->receiveTextBrowser->append("请先关闭连接再改参数");
            return;
        }
        baudSel=4800;
        ui->baudRateButton->setText("4800");
    });
    // ✅ 已修复: 所有下拉菜单都已改成「先判 isOpen, 通过后再 setText」。
    connect(baudRate_9600,&QAction::triggered,this,[=](){
        if(serial->isOpen()){
            ui->receiveTextBrowser->append("请先关闭连接再改参数");
            return;
        }
        baudSel=9600;
        ui->baudRateButton->setText("9600");
    });
    connect(baudRate_115200,&QAction::triggered,this,[=](){
        if(serial->isOpen()){
            ui->receiveTextBrowser->append("请先关闭连接再改参数");
            return;
        }
        baudSel=115200;
        ui->baudRateButton->setText("115200");
    });
    connect(baudRate_230400,&QAction::triggered,this,[=](){
        if(serial->isOpen()){
            ui->receiveTextBrowser->append("请先关闭连接再改参数");
            return;
        }
        baudSel=230400;
        ui->baudRateButton->setText("230400");
    // ✅ 已修复: 已改成 baudSel=230400(不再是 dataSel)。
    });

    dataBitMenu=new QMenu(this);
    dataBit_6=new QAction("6",this);
    dataBit_7=new QAction("7",this);
    dataBit_8=new QAction("8",this);
    dataBitMenu->addAction(dataBit_6);
    dataBitMenu->addAction(dataBit_7);
    dataBitMenu->addAction(dataBit_8);
    ui->dataBitButton->setMenu(dataBitMenu);

    connect(dataBit_6,&QAction::triggered,this,[=](){
        if(serial->isOpen()){
            ui->receiveTextBrowser->append("请先关闭连接再改参数");
            return;
        }
        dataSel=6;
        ui->dataBitButton->setText("6");
    });
    connect(dataBit_7,&QAction::triggered,this,[=](){
        if(serial->isOpen()){
            ui->receiveTextBrowser->append("请先关闭连接再改参数");
            return;
        }
        dataSel=7;
        ui->dataBitButton->setText("7");
    });
    connect(dataBit_8,&QAction::triggered,this,[=](){
        if(serial->isOpen()){
            ui->receiveTextBrowser->append("请先关闭连接再改参数");
            return;
        }
        dataSel=8;
        ui->dataBitButton->setText("8");
    });


    stopBitMenu=new QMenu(this);
    stopBit_1=new QAction("1",this);
    stopBit_2=new QAction("2",this);
    stopBitMenu->addAction(stopBit_1);
    stopBitMenu->addAction(stopBit_2);
    ui->stopBitButton->setMenu(stopBitMenu);

    connect(stopBit_1,&QAction::triggered,this,[=](){
        if(serial->isOpen()){
            ui->receiveTextBrowser->append("请先关闭连接再改参数");
            return;
        }
        stopSel=1;
        ui->stopBitButton->setText("1");
    });
    connect(stopBit_2,&QAction::triggered,this,[=](){
        if(serial->isOpen()){
            ui->receiveTextBrowser->append("请先关闭连接再改参数");
            return;
        }
        stopSel=2;
        ui->stopBitButton->setText("2");
    });

    parityBitMenu=new QMenu(this);
    parityBit_none=new QAction("无校验",this);
    parityBit_odd=new QAction("奇校验",this);
    parityBit_even=new QAction("偶校验",this);
    parityBitMenu->addAction(parityBit_none);
    parityBitMenu->addAction(parityBit_odd);
    parityBitMenu->addAction(parityBit_even);
    ui->parityButton->setMenu(parityBitMenu);

    connect(parityBit_none,&QAction::triggered,this,[=](){
        if(serial->isOpen()){
            ui->receiveTextBrowser->append("请先关闭连接再改参数");
            return;
        }
        paritySel=0;
        ui->parityButton->setText("无校验");
    });
    connect(parityBit_odd,&QAction::triggered,this,[=](){
        if(serial->isOpen()){
            ui->receiveTextBrowser->append("请先关闭连接再改参数");
            return;
        }
        paritySel=3;
        ui->parityButton->setText("奇校验");
    });
    connect(parityBit_even,&QAction::triggered,this,[=](){
        if(serial->isOpen()){
            ui->receiveTextBrowser->append("请先关闭连接再改参数");
            return;
        }
        paritySel=2;
        ui->parityButton->setText("偶校验");
    // ✅ 已修复: 奇校验=3(OddParity)、偶校验=2(EvenParity), 与 QSerialPort::Parity 对上了。
    //   (原来写成 奇=1/偶=2 是错的 —— Parity 里没有值等于 1。)
    });

}

SerialPort::~SerialPort()
{
    delete ui;
}

void SerialPort::closeAll(){        //全部关闭并退回到最初
    //tcp
    ip=QHostAddress(kPeerIp);   // ★本次改动: 取对端默认地址(见文件顶部 kPeerIp)
    port=8888;
    m_mode=MODE_NONE;

    if(netMode==MODE_SERVER){
        if(ui->tcpModeButton->text()=="取消监听"){
            ui->receiveTextBrowser->append("已取消监听");
        }
    ui->tcpModeButton->setText("监听");
    ui->tcpModechoseButton->setText("服务端模式");
    server->close();

    }
    else{
        if(ui->tcpModeButton->text()=="取消连接"){
            ui->receiveTextBrowser->append("已取消连接");
        }
        ui->tcpModeButton->setText("连接");
        ui->tcpModechoseButton->setText("客户端模式");
        if(remoteclient){
            remoteclient->disconnectFromHost();
        }
    }

    setStatus(QStringLiteral("就绪"));
}

// ---- 统一的接收显示入口 ----
// 十六进制 / 时间戳 / 接收开关 / 字节计数都集中在这里，
// 串口和 TCP 两个分支共用，避免各写一份、行为不一致。
void SerialPort::appendData(const QByteArray &data)
{
    rxBytes+=data.size();
    ui->receiveBytesTextLabel->setText(QString::number(rxBytes));

    if(!ui->showReceiveCheckBox->isChecked())
        return;

    QString text=ui->hexCheckBox->isChecked()
                     ? QString::fromLatin1(data.toHex(' '))   // 41 42 0D 0A
                     : QString::fromUtf8(data);

    if(ui->showTimeCheckBox->isChecked()){
        text.prepend(QStringLiteral("[%1] ")
                         .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz"))));
    }

    ui->receiveTextBrowser->moveCursor(QTextCursor::End);
    ui->receiveTextBrowser->insertPlainText(text);
    ui->receiveTextBrowser->ensureCursorVisible();
}

void SerialPort::setStatus(const QString &status)
{
    ui->statusValueLabel->setText(status);
}

void SerialPort::on_ipButton_clicked()
{
    ui->configLineEdit->clear();
    ui->configLineEdit->setPlaceholderText("请输入：ip,例如127.0.0.1");
    m_mode=MODE_IP;
}


void SerialPort::on_tcpPortButton_clicked()
{
    ui->configLineEdit->clear();
    ui->configLineEdit->setPlaceholderText("请输入：端口,例如8888");
    m_mode=MODE_PORT;
}

void SerialPort::onNewClient()
{
    if(localclient) localclient->deleteLater();

    localclient=server->nextPendingConnection();
    // ✅ 已修复: onNewClient() 里先 deleteLater() 旧的 localclient, 不再泄漏。
    //   (仍只支持单客户端, 说明见构造函数里那条。)
    connect(localclient,&QTcpSocket::readyRead,this,&SerialPort::onReadData);
    ui->receiveTextBrowser->append("客户端连接成功");
}

void SerialPort::onReadData()
{
    appendData(localclient->readAll());
}

void SerialPort::onClientRead()
{
    appendData(remoteclient->readAll());
}

void SerialPort::on_confirmButton_clicked()
{
    if(tcp_serialport==MODE_TCP){
    switch (m_mode) {
    case MODE_IP:{
        QString strip=ui->configLineEdit->text().trimmed();
        QHostAddress  addr(strip);
        if(!addr.isNull()){
            ip=addr;
            ui->configLineEdit->clear();
            ui->receiveTextBrowser->append("成功绑定ip");
        }
        else{
            ui->configLineEdit->clear();
            ui->configLineEdit->setPlaceholderText("绑定失败，请再次点击ip选项输入正确ip");
            ui->receiveTextBrowser->append("绑定ip失败");

        }
        break;
    }
    case MODE_PORT:
    {
        QString strport=ui->configLineEdit->text().trimmed();
        bool ok=false;
        quint32 tempport=strport.toUInt(&ok);
        if(ok&&tempport<=65535&&tempport>0){
            ui->receiveTextBrowser->append("成功绑定端口号");
            ui->configLineEdit->clear();
            port=tempport;
        }
        else{

            ui->configLineEdit->clear();
            ui->configLineEdit->setPlaceholderText("绑定失败，请再次点击端口选项输入正确端口号");
            ui->receiveTextBrowser->append("绑定端口号失败");

        }
        break;
    }
    case MODE_NONE:
        ui->configLineEdit->setPlaceholderText("请先选择类型绑定");

        break;
    default:
        ui->configLineEdit->setPlaceholderText("请先选择类型绑定");
        break;
    }
    }
    else{
        ui->receiveTextBrowser->append("当前为串口模式，绑定失败，请选择TCP模式后重试");
        return;
    }

}


void SerialPort::on_tcpModeButton_clicked()
{
    if(tcp_serialport==MODE_TCP){
    if(server->isListening()){
        server->close();
        ui->receiveTextBrowser->append(QStringLiteral("已取消监听"));
        ui->tcpModeButton->setText(QStringLiteral("监听"));
        setStatus(QStringLiteral("就绪"));
        return;
    }
    if(netMode==MODE_SERVER){
    // ★本次改动: 原为 QHostAddress(ip) —— 拿"对端地址"当监听地址是错的,
    //   那地址不属于本机, listen 必然失败。服务端一律监听 0.0.0.0(所有网卡)。
    bool listenok=server->listen(QHostAddress::Any,port);
    if(listenok){
        ui->receiveTextBrowser->append(QStringLiteral("监听端口成功 %1").arg(port));
        ui->tcpModeButton->setText(QStringLiteral("取消监听"));
        setStatus(QStringLiteral("正在监听 0.0.0.0:%1").arg(port));
    }
    else{
        // 失败时按钮要退回"监听"，否则用户没法再点第二次
        ui->tcpModeButton->setText(QStringLiteral("监听"));
        const QString error=server->errorString();
        ui->receiveTextBrowser->append(error);
        ui->receiveTextBrowser->append(QStringLiteral("监听端口失败 %1").arg(port));
        setStatus(QStringLiteral("监听失败：")+error);
        }
    }
    else if(netMode==MODE_CLIENT){
        // 用 socket 的真实状态判断，而不是看按钮文字 —— 文字会被
        // connected / disconnected / errorOccurred 三个回调改写，容易不同步。
        if(remoteclient->state()!=QAbstractSocket::UnconnectedState){
            remoteclient->abort();
            ui->receiveTextBrowser->append(QStringLiteral("已取消连接"));
            ui->tcpModeButton->setText(QStringLiteral("连接"));
            setStatus(QStringLiteral("就绪"));
        }
        else{
            remoteclient->connectToHost(ip,port);
            ui->receiveTextBrowser->append(QStringLiteral("正在发起连接"));
            ui->tcpModeButton->setText(QStringLiteral("取消连接"));
            setStatus(QStringLiteral("正在连接 %1:%2").arg(ip.toString()).arg(port));
        }
    }
    }
    else{
        ui->receiveTextBrowser->append("当前为串口模式，选择失败，请选择TCP模式后重试");
        return;
    }
}


void SerialPort::on_clearReceiveButton_clicked()
{
    ui->receiveTextBrowser->clear();
    rxBytes=0;
    ui->receiveBytesTextLabel->setText(QStringLiteral("0"));
}


void SerialPort::on_tcpModechoseButton_clicked()
{
    if(tcp_serialport==MODE_TCP){
    server->close();
    if(remoteclient){
        remoteclient->disconnectFromHost();
    }
    switch (netMode) {
    case MODE_SERVER:
        if(ui->tcpModeButton->text()=="取消监听"){
            ui->receiveTextBrowser->append("已取消监听");
        }
        ui->tcpModeButton->setText("连接");
        ui->tcpModechoseButton->setText("客户端模式");
        netMode=MODE_CLIENT;
        break;
    case MODE_CLIENT:
        if(ui->tcpModeButton->text()=="取消连接"){
            ui->receiveTextBrowser->append("已取消连接");
        }
        ui->tcpModeButton->setText("监听");
        ui->tcpModechoseButton->setText("服务端模式");
        netMode=MODE_SERVER;
        break;
    default:
        break;
    }
    }
    else{
        ui->receiveTextBrowser->append("当前为串口模式，选择失败，请选择TCP模式后重试");
        // return;
    }

}


void SerialPort::on_sendButton_clicked()
{
    QString data=ui->sendTextEdit->toPlainText();
    // ★本次改动: 发送时补行尾 \r\n —— 原代码把你输入的原文原样发出、不带任何换行,
    //   对端连续收到两段就会粘成一行。补上行尾后, 对端每段自然各占一行。
    QByteArray payload=(data+"\r\n").toUtf8();
    // ✅ 已修: 发送已按 tcp_serialport 分流, 串口模式下会写 serial
      if(tcp_serialport==MODE_SERIALPORT){
          if(serial->isOpen()){ serial->write(payload);
                                ui->receiveTextBrowser->append(QString("发送：%1").arg(data)); }
          else ui->receiveTextBrowser->append("发送失败，串口未打开");
          return;
      }

    if(netMode==MODE_CLIENT){
        if(remoteclient!=nullptr&&remoteclient->state()==QTcpSocket::ConnectedState){
            remoteclient->write(payload);   // ★本次改动: 原为 write(data.toUtf8())
            ui->receiveTextBrowser->append(QString("发送：%1").arg(data));   // ★本次改动: 原为 .arg(data.toUtf8()), Qt5 下重载有歧义
        }
        else{
            ui->receiveTextBrowser->append("发送失败，已断开连接，请检查是否被监听或连接成功");
        }
    }
    else if(netMode==MODE_SERVER){
        if(localclient!=nullptr&&localclient->state()==QTcpSocket::ConnectedState){
            localclient->write(payload);   // ★本次改动: 原为 write(data.toUtf8())
            ui->receiveTextBrowser->append(QString("发送：%1").arg(data));   // ★本次改动: 原为 .arg(data.toUtf8()), Qt5 下重载有歧义
        }
        else{
            ui->receiveTextBrowser->append("发送失败，已断开连接，请检查是否被连接或监听成功");
        }
    }
}

void SerialPort::on_modeSwitchButton_clicked()
{
    closeAll();
    switch (tcp_serialport) {
    case MODE_TCP:
        ui->modeSwitchButton->setText(QStringLiteral("串口模式"));
        tcp_serialport=MODE_SERIALPORT;
        setStatus(QStringLiteral("已切换到串口模式"));
        break;
    case MODE_SERIALPORT:
        ui->modeSwitchButton->setText(QStringLiteral("TCP模式"));
        tcp_serialport=MODE_TCP;
        setStatus(QStringLiteral("已切换到 TCP 模式"));
        break;
    default:
        break;
    }

}

//串口






void SerialPort::on_connectButton_clicked()
{
    if(tcp_serialport==MODE_SERIALPORT){
    // ✅ 已修复: 关闭分支里已补 ui->connectButton->setText("连接"); 按钮文字不再错乱。
    if(serial->isOpen()){
        serial->close();
        ui->connectButton->setText(QStringLiteral("连接"));
        setStatus(QStringLiteral("串口已关闭"));
        return;
    }
    if(ui->connectButton->text()==QStringLiteral("连接")){
    serial->setPortName(portName);   // 端口名来自 portButton 菜单（热插拔枚举），不再硬编码
    // setXxx 返回 false 表示该参数不被驱动接受，必须停下并提示，
    // 否则会带着错误参数继续 open()，问题更难定位。
    if(!serial->setBaudRate(baudSel)
        ||!serial->setDataBits(static_cast<QSerialPort::DataBits>(dataSel))
        ||!serial->setStopBits(static_cast<QSerialPort::StopBits>(stopSel))
        ||!serial->setParity(static_cast<QSerialPort::Parity>(paritySel))){
        ui->receiveTextBrowser->append(QStringLiteral("串口参数设置失败：")+serial->errorString());
        setStatus(QStringLiteral("参数设置失败"));
        return;
    }
    bool ok=serial->open(QIODevice::ReadWrite);
    if(!ok){
        ui->receiveTextBrowser->append(QStringLiteral("打开失败：")+serial->errorString());
        setStatus(QStringLiteral("打开失败：")+serial->errorString());
        return;
    }
    ui->connectButton->setText(QStringLiteral("取消连接"));
    setStatus(QStringLiteral("串口已打开：")+portName);
    }
    // else{
    //     serial->close();
    //     ui->connectButton->setText("连接");
    // }
    }
    else{
        ui->receiveTextBrowser->append("当前为TCP模式，选择失败，请选择端口模式后重试");
        // return;
    }
}

