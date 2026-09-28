#ifndef MUSICDIALOG_H
#define MUSICDIALOG_H

#include <QDialog>

namespace Ui {
class MusicDialog;
}

class MusicDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MusicDialog(QWidget *parent = nullptr);
    ~MusicDialog();

private slots:
    void on_btnBack_clicked();

private:
    Ui::MusicDialog *ui;
};

#endif // MUSICDIALOG_H
