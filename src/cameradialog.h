#ifndef CAMERADIALOG_H
#define CAMERADIALOG_H

#include <QDialog>

namespace Ui {
class CameraDialog;
}

class CameraDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CameraDialog(QWidget *parent = nullptr);
    ~CameraDialog();

private slots:
    void on_btnBack_clicked();

private:
    Ui::CameraDialog *ui;
};

#endif // CAMERADIALOG_H
