#include "homedialog.h"
#include "ui_HomeDialog.h"

HomeDialog::HomeDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::HomeDialog)
{
    ui->setupUi(this);
}

HomeDialog::~HomeDialog()
{
    delete ui;
}

void HomeDialog::on_btnBack_clicked()
{
    this->close();
}
