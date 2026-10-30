#ifndef BLUETOOTHDIALOG_H
#define BLUETOOTHDIALOG_H

#include <QDialog>
#include <QThread>
#include <QString>
#include <QStringList>
#include <bluettothworker.h>
#include <QListWidgetItem>

namespace Ui {
class BluetoothDialog;
}

class BluetoothDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BluetoothDialog(QWidget *parent = nullptr);
    ~BluetoothDialog();

signals:
    void connectedReceived(const BluetoothDevice &device);
    void scanReceived();
    void startReceived();
    void stopReceived();

private slots:
    void on_btnBack_clicked();

    void on_btnScan_clicked();

    void on_btnConnect_clicked();

    void on_checkBoxBluetooth_toggled(bool checked);

    void on_listWidgetConnected_itemClicked(QListWidgetItem *item);

    void handleConnectedFinished(bool success=false);
    void handleScanFinished();
    void handleFoundDevice(const QList<BluetoothDevice> &devices);
    void handleStatusChanged(const QString &status);


private:
    Ui::BluetoothDialog *ui;
    bluettothworker *worker;
    QThread workerThread;
    BluetoothDevice *selectedDevice=nullptr;
    bool isScanning={false};
    bool isConnecting={false};

};

#endif // BLUETOOTHDIALOG_H
