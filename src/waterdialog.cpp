#include "waterdialog.h"
#include "ui_WaterDialog.h"

WaterDialog::WaterDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::WaterDialog)
{
    ui->setupUi(this);
}

WaterDialog::~WaterDialog()
{
    delete ui;
}

void WaterDialog::on_btnBack_clicked()
{
    this->close();
}
