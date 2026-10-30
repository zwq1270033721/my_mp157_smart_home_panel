#ifndef LIGHTDIALOG_H
#define LIGHTDIALOG_H

#include <QDialog>

namespace Ui {
class LightDialog;
}

class LightDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LightDialog(QWidget *parent = nullptr);
    ~LightDialog();

private slots:
    void on_btnBack_clicked();

    void on_btnLightOn_clicked();

    void on_btnLightOff_clicked();

private:
    Ui::LightDialog *ui;
};

#endif // LIGHTDIALOG_H
