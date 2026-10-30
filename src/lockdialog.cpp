#include "lockdialog.h"
#include "ui_LockDialog.h"

LockDialog::LockDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LockDialog)
{
    ui->setupUi(this);
}

LockDialog::~LockDialog()
{
    delete ui;
}

void LockDialog::on_btnBack_clicked()
{
    this->close();
}
