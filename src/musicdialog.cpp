#include "musicdialog.h"
#include "ui_MusicDialog.h"

MusicDialog::MusicDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::MusicDialog)
{
    ui->setupUi(this);
}

MusicDialog::~MusicDialog()
{
    delete ui;
}

void MusicDialog::on_btnBack_clicked()
{
    this->close();
}
