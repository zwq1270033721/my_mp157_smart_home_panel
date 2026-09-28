#include "weatherdialog.h"
#include "ui_WeatherDialog.h"

WeatherDialog::WeatherDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::WeatherDialog)
{
    ui->setupUi(this);
}

WeatherDialog::~WeatherDialog()
{
    delete ui;
}

void WeatherDialog::on_btnBack_clicked()
{
    this->close();
}
