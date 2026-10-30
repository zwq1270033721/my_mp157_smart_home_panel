#include "tvdialog.h"
#include "ui_TvDialog.h"

TvDialog::TvDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::TvDialog)
{
    ui->setupUi(this);
}

TvDialog::~TvDialog()
{
    delete ui;
}

void TvDialog::on_btnBack_clicked()
{
    this->close();
}
