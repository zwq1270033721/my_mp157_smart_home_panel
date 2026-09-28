#include "cameradialog.h"
#include "ui_CameraDialog.h"

CameraDialog::CameraDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::CameraDialog)
{
    ui->setupUi(this);
}

CameraDialog::~CameraDialog()
{
    delete ui;
}

void CameraDialog::on_btnBack_clicked()
{
    this->close();
}
