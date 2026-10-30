#ifndef QQDIALOG_H
#define QQDIALOG_H

#include <QDialog>
#include <QListWidgetItem>
namespace Ui {
class QqDialog;
}

class QqDialog : public QDialog
{
    Q_OBJECT

public:
    explicit QqDialog(QWidget *parent = nullptr);
    ~QqDialog();

private slots:
    void on_btnBack_clicked();

    void on_btnLogin_clicked();

    void on_btnMenu_clicked();

    void on_listFriends_itemClicked(QListWidgetItem *item);

    void on_btnSend_clicked();

    void on_btnClose_clicked();

    void on_btnToolEmoji_clicked();

    void on_listEmojis_itemClicked(QListWidgetItem *item);

private:
    Ui::QqDialog *ui;
};

#endif // QQDIALOG_H
