#include "lightdialog.h"
#include "ui_LightDialog.h"
#include  <QFile>
#include <QDebug>
LightDialog::LightDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LightDialog)
{
    ui->setupUi(this);
}

LightDialog::~LightDialog()
{
    delete ui;
}

void LightDialog::on_btnBack_clicked()
{
    this->close();
}

void LightDialog::on_btnLightOn_clicked()
{
    QFile ledFile("/sys/class/leds/user-led/brightness");
    if(ledFile.open(QIODevice::WriteOnly)){
        ledFile.write("1");
        ledFile.close();

    }
    else{
        qDebug()<<"文件打开失败，检查路径/权限";


    }
}


void LightDialog::on_btnLightOff_clicked()
{
    QFile ledFile("/sys/class/leds/user-led/brightness");
    if(ledFile.open(QIODevice::WriteOnly)){
        ledFile.write("0");
        ledFile.close();

    }
    else{
        qDebug()<<"文件打开失败，检查路径/权限";


    }
}

