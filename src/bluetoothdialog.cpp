#include "bluetoothdialog.h"
#include "ui_BluetoothDialog.h"

BluetoothDialog::BluetoothDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::BluetoothDialog)
{
    ui->setupUi(this);
}

BluetoothDialog::~BluetoothDialog()
{
    delete ui;
}

void BluetoothDialog::on_btnBack_clicked()
{
    this->close();
}
