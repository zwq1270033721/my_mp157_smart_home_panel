#include "sundialog.h"
#include "ui_SunDialog.h"

SunDialog::SunDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SunDialog)
{
    ui->setupUi(this);
}

SunDialog::~SunDialog()
{
    delete ui;
}

void SunDialog::on_btnBack_clicked()
{
    this->close();
}
