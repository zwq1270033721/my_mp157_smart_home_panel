#ifndef MOONDIALOG_H
#define MOONDIALOG_H

#include <QDialog>

namespace Ui {
class MoonDialog;
}

class MoonDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MoonDialog(QWidget *parent = nullptr);
    ~MoonDialog();

private slots:
    void on_btnBack_clicked();

private:
    Ui::MoonDialog *ui;
};

#endif // MOONDIALOG_H
