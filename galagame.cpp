#include "galagame.h"
#include "ui_galagame.h"

galagame::galagame(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::galagame)
{
    ui->setupUi(this);
}

galagame::~galagame()
{
    delete ui;
}
