#ifndef SUNDIALOG_H
#define SUNDIALOG_H

#include <QDialog>

namespace Ui {
class SunDialog;
}

class SunDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SunDialog(QWidget *parent = nullptr);
    ~SunDialog();

private slots:
    void on_btnBack_clicked();

private:
    Ui::SunDialog *ui;
};

#endif // SUNDIALOG_H
