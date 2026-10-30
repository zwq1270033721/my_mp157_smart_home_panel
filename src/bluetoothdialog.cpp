#include "bluetoothdialog.h"
#include "ui_BluetoothDialog.h"
#include <QListWidgetItem>
BluetoothDialog::BluetoothDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::BluetoothDialog)
{
    ui->setupUi(this);
    qRegisterMetaType<BluetoothDevice>("BluetoothDevice");
    // foundDevice 是跨线程信号, 排队连接需要容器本身的元类型,
    // 只注册元素类型不够, 否则 Qt 会丢弃参数并打印
    // "Cannot queue arguments of type 'QList<BluetoothDevice>'"
    qRegisterMetaType<QList<BluetoothDevice>>("QList<BluetoothDevice>");
    selectedDevice=new BluetoothDevice();
    ui->btnScan->setEnabled(false);
    ui->btnConnect->setEnabled(false);

    worker = new bluettothworker();
    worker->moveToThread(&workerThread);
    
    connect(&workerThread, &QThread::finished, worker, &QObject::deleteLater);

    connect(worker, &bluettothworker::connectedFinished, this, &BluetoothDialog::handleConnectedFinished);
    connect(worker, &bluettothworker::scanFinished, this, &BluetoothDialog::handleScanFinished);
    connect(worker, &bluettothworker::foundDevice, this, &BluetoothDialog::handleFoundDevice);
    connect(worker, &bluettothworker::statusChanged, this, &BluetoothDialog::handleStatusChanged);

    connect(this, &BluetoothDialog::connectedReceived, worker, &bluettothworker::connect);
    connect(this, &BluetoothDialog::scanReceived, worker, &bluettothworker::scan);
    connect(this, &BluetoothDialog::startReceived, worker, &bluettothworker::start);
    connect(this, &BluetoothDialog::stopReceived, worker, &bluettothworker::stop);

    
    workerThread.start();
}

BluetoothDialog::~BluetoothDialog()
{
    delete ui;
    worker->requestStop();   // 打断 worker 里可能正在跑的 bluetoothctl, 让线程能退出
    workerThread.quit();
    workerThread.wait();
}

void BluetoothDialog::handleConnectedFinished(bool success){
    isConnecting=false;
    if(success){
        ui->btnConnect->setText("已连接");
        ui->btnConnect->setEnabled(false);
        ui->lblStatusValue->setText("已连接");
    }
    else{
        ui->btnConnect->setText("连接");    
        ui->btnConnect->setEnabled(true);
        ui->lblStatusValue->setText("连接失败");
    }
}



void BluetoothDialog::handleScanFinished(){
    isScanning=false;
    ui->btnScan->setText("扫描");
    ui->btnScan->setEnabled(true);
}


void BluetoothDialog::handleFoundDevice(const QList<BluetoothDevice> &devices){
    if(devices.isEmpty()){
        ui->lblStatusValue->setText("未发现设备");
        return;
    }
    ui->listWidgetConnected->clear();
    for (const auto &device : devices) {
        auto item = new QListWidgetItem(device.name,ui->listWidgetConnected);
        item->setData(Qt::UserRole,device.address);
        ui->listWidgetConnected->addItem(item);
    }
    ui->lblStatusValue->setText(QString("发现 %1 个设备").arg(devices.count()));
}



void BluetoothDialog::handleStatusChanged(const QString &status){
    ui->lblStatusValue->setText(status);
}














void BluetoothDialog::on_btnBack_clicked()
{
    this->close();
}

void BluetoothDialog::on_btnScan_clicked()
{
    if(isScanning){
        return;
    }
    isScanning=true;
    ui->btnScan->setEnabled(false);
    ui->btnScan->setText("扫描中...");
    emit scanReceived();
}


void BluetoothDialog::on_btnConnect_clicked()
{
    if(isConnecting){
        return;
    }
    if(selectedDevice->address.isEmpty()){
        return;
    }
    isConnecting=true;
    ui->btnConnect->setEnabled(false);
    ui->btnConnect->setText("连接中...");
    emit connectedReceived(*selectedDevice);
}


void BluetoothDialog::on_checkBoxBluetooth_toggled(bool checked)
{
    if(checked){
        startReceived();
        ui->btnScan->setEnabled(true);
        ui->btnConnect->setEnabled(true);
    }else{
        isConnecting=false;
        // 注意: selectedDevice 是指针, 不能置空, 否则再点列表项会空指针崩溃
        selectedDevice->name.clear();
        selectedDevice->address.clear();
        ui->lblDeviceName->setText("");
        ui->lblMacAddress->setText("");
        ui->lblSelectedDeviceValue->setText("");
        ui->lblTargetMacValue->setText("");
        stopReceived();
        ui->btnScan->setEnabled(false);
        ui->btnScan->setText("扫描");
        ui->btnConnect->setEnabled(false);
        ui->btnConnect->setText("连接");
        ui->lblStatusValue->setText("未连接");
    }
}


void BluetoothDialog::on_listWidgetConnected_itemClicked(QListWidgetItem *item)
{
    selectedDevice->name=item->text();
    selectedDevice->address=item->data(Qt::UserRole).toString();
    ui->lblDeviceName->setText(item->text());
    ui->lblMacAddress->setText(item->data(Qt::UserRole).toString());
    ui->lblSelectedDeviceValue->setText(item->data(Qt::UserRole).toString());
    ui->lblTargetMacValue->setText(item->data(Qt::UserRole).toString());

}

