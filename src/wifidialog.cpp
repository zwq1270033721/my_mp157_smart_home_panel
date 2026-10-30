#include "wifidialog.h"
#include "ui_WifiDialog.h"

WifiDialog::WifiDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::WifiDialog)
{
    ui->setupUi(this);
}

WifiDialog::~WifiDialog()
{
    delete ui;
}

void WifiDialog::on_btnBack_clicked()
{
    this->close();
}
