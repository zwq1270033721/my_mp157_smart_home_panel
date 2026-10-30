#ifndef WIFIDIALOG_H
#define WIFIDIALOG_H

#include <QDialog>

namespace Ui {
class WifiDialog;
}

class WifiDialog : public QDialog
{
    Q_OBJECT

public:
    explicit WifiDialog(QWidget *parent = nullptr);
    ~WifiDialog();

private slots:
    void on_btnBack_clicked();

private:
    Ui::WifiDialog *ui;
};

#endif // WIFIDIALOG_H
