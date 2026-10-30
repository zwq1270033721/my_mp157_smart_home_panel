#include "moondialog.h"
#include "ui_MoonDialog.h"

MoonDialog::MoonDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::MoonDialog)
{
    ui->setupUi(this);
}

MoonDialog::~MoonDialog()
{
    delete ui;
}

void MoonDialog::on_btnBack_clicked()
{
    this->close();
}
