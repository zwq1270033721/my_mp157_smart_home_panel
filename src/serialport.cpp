#include "serialport.h"
#include "ui_serialport.h"
#include "keyboardialog.h"
#include "softkeyboardbinder.h"
#include <QTextCursor>
#include <QDebug>
#include <QHostAddress>
#include <QLineEdit>
#include <QTextEdit>
SerialPort::SerialPort(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SerialPort)
{
    ui->setupUi(this);

    // 给这两个输入框绑定软键盘（点击自动弹出，自动区分写进哪个框）
    SoftKeyboardBinder::attach(this, {ui->textEdit, ui->chooseinput});

    connect(this,&SerialPort::ModeChanged,this,&SerialPort::slot_changeUi);
    setAttribute(Qt::WA_TranslucentBackground);

    // 背景标签压到最底层，不挡住 textEdit 的点击
    ui->label->lower();
    ui->label_4->lower();
    
    ui->chooseinput->setVisible(false);
    ui->yes->setVisible(false);
    
    
}

SerialPort::~SerialPort()
{
    delete ui;
}

void SerialPort::on_pushButton_8_clicked()
{
    this->reject();
}


void SerialPort::on_sendpushbutton_clicked()
{

    QString text="sender:";
    text+=ui->textEdit->toPlainText();
    if(!text.isEmpty()){
        ui->textEdit->clear();
        ui->textBrowser->setText(text);
    }
}


void SerialPort::on_pushButton_6_clicked()
{
    ui->textBrowser->clear();
}

void SerialPort::on_modeChanged_clicked()
{
    isSerialMode=!isSerialMode;
    emit ModeChanged(isSerialMode);
    if(isSerialMode){
        ui->modeChanged->setText("串口模式");

    }
    else{
        ui->modeChanged->setText("TCP模式");
    }
}

void SerialPort::slot_changeUi(bool isSerial){
    if(isSerial){
        qDebug()<<"进入";

        ui->serialPort->setEnabled(true);
        ui->baudrate->setEnabled(true);
        ui->stopBit->setEnabled(true);
        ui->dataBit->setEnabled(true);
        ui->parity->setEnabled(true);

        ui->tcpMode->setEnabled(false);
        ui->tcpPort->setEnabled(false);
        ui->tcpip->setEnabled(false);

    }
    else{
        qDebug()<<"退出";

        ui->serialPort->setEnabled(false);
        ui->baudrate->setEnabled(false);
        ui->stopBit->setEnabled(false);
        ui->dataBit->setEnabled(false);
        ui->parity->setEnabled(false);

        ui->tcpMode->setEnabled(true);
        ui->tcpPort->setEnabled(true);
        ui->tcpip->setEnabled(true);
    }
}
// 软键盘逻辑已全部抽到 SoftKeyboardBinder，见构造函数里的 attach()




void SerialPort::on_tcpip_clicked()
{
    m_inputMode=INPUT_IP;
    
    ui->chooseinput->setVisible(true);
    ui->yes->setVisible(true);
    
    ui->chooseinput->setFocus();
    
    ui->chooseinput->setPlaceholderText("请输入IP地址，例如127.0.0.1");
}


void SerialPort::on_tcpPort_clicked()
{
    m_inputMode=INPUT_PORT;
    ui->chooseinput->setVisible(true);
    ui->yes->setVisible(true);
    ui->chooseinput->setFocus();
    ui->chooseinput->setPlaceholderText("请输入端口号，例如范围1-665535");
    
}


void SerialPort::on_tcpMode_clicked()
{
    
}


void SerialPort::on_yes_clicked()
{
    QString inputText=ui->chooseinput->text().trimmed();
    if((m_inputMode==INPUT_IP)){
        QHostAddress ipAddr(inputText);
        if((ipAddr.isNull())){
            qDebug()<<"IP地址格式错误!";
            return;
        }
        m_stargetIp=inputText;;
        qDebug()<<"保存IP："<<m_stargetIp;
    }
    else if(m_inputMode==INPUT_PORT){
        bool ok;
        quint16 portNum=inputText.toUShort(&ok);
        if(ok&&portNum>=1){
            m_stargetPort=portNum;
            qDebug()<<"保存端口:"<<portNum;

        }
        else{
            qDebug()<<"端口号输入非法！必须是1-65535 数字" ;
            return;
        }
    }
    ui->chooseinput->setVisible(false);
    ui->yes->setVisible(false);
    m_inputMode=NONE;

}


void SerialPort::on_chooseinput_returnPressed()
{
    on_yes_clicked();
}

bool SerialPort::setSerialConfig(int fd,int baud,int databit,int stopbit,int paritybit)
{
    struct termios opt;
    if(tcgetattr(fd,&opt)<0){
        qDebug()<<"tcgetattr error";
        return false;
    }
    opt.c_cflag &=~(CSIZE|CBAUD);
    opt.c_lflag &=~(ICANON|ECHO|ECHONL|ISIG);
    opt.c_iflag &=~(INPCK|ICRNL);
    opt.c_oflag &=~(OPOST);

    speed_t speed;
    switch(baud){
    case 115200:
        speed=B115200;
        break;
    case 19200:
        speed=B19200;
        break;
    case 38400:
        speed=B38400;
        break;
    case 57600:
        speed=B57600;
        break;
    default:
        speed=B9600;
        break;
    }
    cfsetispeed(&opt,speed);
    cfsetospeed(&opt,speed);
    if(databit==5){
        opt.c_cflag |= CS5;
    }
    else if(databit==6){
        opt.c_cflag |= CS6;
    }
    else if(databit==7){
        opt.c_cflag |= CS7;
    }
    else if(databit==8){
        opt.c_cflag |= CS8;
    }
    else{
        opt.c_cflag |= CS8;
    }
    if(stopbit==1){
        opt.c_cflag |= CSTOPB;
    }
    else if(stopbit==2){
        opt.c_cflag |= CSTOPB;
    }
    else{
        opt.c_cflag |= CSTOPB;
    }
    if(paritybit==0){
        opt.c_cflag &=~(PARENB|PARODD);
    }
    else if(paritybit==1){
        opt.c_cflag |= PARENB;
    }
    else if(paritybit==2){
        opt.c_cflag |= PARENB;
        opt.c_cflag |= PARODD;
    }
    else{
        opt.c_cflag &=~(PARENB|PARODD);
    }
    if(tcsetattr(fd,TCSANOW,&opt)<0){
        qDebug()<<"tcsetattr error";
        return false;
    }
    return true;
    if(stopbit==1){
        opt.c_cflag &=~CSTOPB;
    }
    else if(stopbit==2){
        opt.c_cflag |= CSTOPB;
    }
    else{
        opt.c_cflag &=~CSTOPB;
    }
    if(paritybit==0){
        opt.c_cflag &=~(PARENB|PARODD);
    }
    else if(paritybit==1){
        opt.c_cflag |= (PARENB|PARODD);
    }
    else if(paritybit==2){
        opt.c_cflag |= (PARENB&~PARODD);
    }
    else{
        opt.c_cflag &=~(PARENB|PARODD);
    }
    opt.c_cc[VMIN]=1;
    opt.c_cc[VTIME]=0;
    if(tcsetattr(fd,TCSANOW,&opt)<0){
        qDebug()<<"tcsetattr error";
        return false;
    }
    return true;
}

void SerialPort::on_serialPort_clicked()
{

}


void SerialPort::on_baudrate_clicked()
{
    QMenu *menu=new QMenu(this);
    menu->addAction("1200");
    menu->addAction("19200");
    menu->addAction("38400");
    menu->addAction("57600");
    menu->addAction("115200");
    
    QAction *act=menu->exec(ui->baudrate->mapToGlobal(QPoint(0,ui->baudrate->height())));
    if(act){
        QString txt="波特：";
            txt+=act->text();
        ui->baudrate->setText(txt);
        m_baud=txt.toInt();

    }
    delete menu;
}


void SerialPort::on_dataBit_clicked()
{
    QMenu *menu=new QMenu(this);
    menu->addAction("5");
    menu->addAction("6");
    menu->addAction("7");
    menu->addAction("8");
    QAction *act=menu->exec(ui->dataBit->mapToGlobal(QPoint(0,ui->dataBit->height())));
    if(act){
        QString txt="数据位：";
            txt+=act->text();
        ui->dataBit->setText(txt);
        if (txt=="5") {
            m_dataBit=5;
        }
        else if(txt=="6"){
            m_dataBit=6;
        }
        else if(txt=="7"){
            m_dataBit=7;
        }
        else if(txt=="8"){
            m_dataBit=8;
        }

    }
    delete menu;

}


void SerialPort::on_stopBit_clicked()
{
    QMenu *menu=new QMenu(this);
    menu->addAction("1");
    menu->addAction("1.5");
    menu->addAction("2");
    QAction *act=menu->exec(ui->stopBit->mapToGlobal(QPoint(0,ui->stopBit->height())));
    if(act){
        QString txt="停止位：";
        txt+=act->text();
        ui->stopBit->setText(txt);
        if (txt=="1") {
            m_stopBit=1;
        }
        else if(txt=="1.5"){
            m_stopBit=3;
        }
        else if(txt=="2"){
            m_stopBit=2;
        }
    }
    delete menu;
}


void SerialPort::on_parity_clicked()
{
    QMenu *menu=new QMenu(this);
    menu->addAction("无校验");
    menu->addAction("奇校验");
    menu->addAction("偶校验");
    QAction *act=menu->exec(ui->parity->mapToGlobal(QPoint(0,ui->parity->height())));
    if(act){
        QString txt="校验位：";
        txt+=act->text();
        ui->parity->setText(txt);
        if (txt=="无校验") {
            m_parity=0;
        }
        else if(txt=="奇校验"){
            m_parity=1;
        }
        else if(txt=="偶校验"){
            m_parity=2;
        }
    }
    delete menu;
}

