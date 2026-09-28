#ifndef TVDIALOG_H
#define TVDIALOG_H

#include <QDialog>

namespace Ui {
class TvDialog;
}

class TvDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TvDialog(QWidget *parent = nullptr);
    ~TvDialog();

private slots:
    void on_btnBack_clicked();

private:
    Ui::TvDialog *ui;
};

#endif // TVDIALOG_H
