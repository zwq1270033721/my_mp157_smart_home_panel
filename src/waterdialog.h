#ifndef WATERDIALOG_H
#define WATERDIALOG_H

#include <QDialog>

namespace Ui {
class WaterDialog;
}

class WaterDialog : public QDialog
{
    Q_OBJECT

public:
    explicit WaterDialog(QWidget *parent = nullptr);
    ~WaterDialog();

private slots:
    void on_btnBack_clicked();

private:
    Ui::WaterDialog *ui;
};

#endif // WATERDIALOG_H
