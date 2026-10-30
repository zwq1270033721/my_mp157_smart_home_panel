#include "wifidialog.h"
#include "ui_WifiDialog.h"

#include "softkeyboardbinder.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QThread>

WifiDialog::WifiDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::WifiDialog)
{
    ui->setupUi(this);

    // 板子没有物理键盘，密码框必须能弹出软键盘
    SoftKeyboardBinder::attach(this, ui->passwordLineEdit);

    // 跨线程传递自定义类型之前必须注册（Qt5 的队列连接尤其需要）
    qRegisterMetaType<WifiInfo>("WifiInfo");
    qRegisterMetaType<QList<WifiInfo>>("QList<WifiInfo>");

    // ---- 工作线程 ----
    m_thread = new QThread(this);
    m_tool   = new WifiTool;                 // 无 parent，生命周期交给线程
    m_tool->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_tool, &QObject::deleteLater);

    connect(this, &WifiDialog::scanRequested,       m_tool, &WifiTool::scan);
    connect(this, &WifiDialog::connectRequested,    m_tool, &WifiTool::connectTo);
    connect(this, &WifiDialog::disconnectRequested, m_tool, &WifiTool::disconnectWifi);
    connect(this, &WifiDialog::stateRequested,      m_tool, &WifiTool::queryCurrentState);

    connect(m_tool, &WifiTool::statusChanged,     this, &WifiDialog::onStatus);
    connect(m_tool, &WifiTool::scanFinished,      this, &WifiDialog::onScanFinished);
    connect(m_tool, &WifiTool::connectFinished,   this, &WifiDialog::onConnectFinished);
    connect(m_tool, &WifiTool::disconnectFinished,this, &WifiDialog::onDisconnected);
    connect(m_tool, &WifiTool::currentState,      this, &WifiDialog::onCurrentState);

    m_thread->start();

    ui->connectedlabel->setText(QStringLiteral("正在读取当前网络..."));
    emit stateRequested();
}

WifiDialog::~WifiDialog()
{
    // 先中止正在跑的 iw / wpa_supplicant / udhcpc，否则关闭对话框会卡到命令超时
    if (m_tool)
        m_tool->abort();

    if (m_thread && m_thread->isRunning()) {
        m_thread->quit();
        if (!m_thread->wait(5000)) {
            m_thread->terminate();
            m_thread->wait(1000);
        }
    }

    delete ui;
}

void WifiDialog::on_btnBack_clicked()
{
    reject();
}

void WifiDialog::on_refreshButton_clicked()
{
    setBusy(true);
    ui->refreshButton->setText(QStringLiteral("扫描中..."));
    ui->connectedlabel->setText(QStringLiteral("正在扫描..."));
    emit scanRequested();
}

void WifiDialog::on_connectButton_clicked()
{
    if (m_connected) {
        setBusy(true);
        ui->connectButton->setText(QStringLiteral("断开中..."));
        emit disconnectRequested();
        return;
    }

    if (m_selectedSsid.isEmpty()) {
        ui->connectedlabel->setText(QStringLiteral("请先在左侧选择一个热点"));
        return;
    }

    setBusy(true);
    ui->connectButton->setText(QStringLiteral("连接中..."));
    ui->connectedlabel->setText(QStringLiteral("正在连接 %1 ...").arg(m_selectedSsid));
    emit connectRequested(m_selectedSsid, ui->passwordLineEdit->text());
}

void WifiDialog::on_wifiListWidget_itemClicked(QListWidgetItem *item)
{
    if (!item)
        return;

    m_selectedSsid = item->data(Qt::UserRole).toString();
    if (m_selectedSsid.isEmpty())
        m_selectedSsid = item->text();

    ui->signalValueLabel->setText(signalText(item->data(Qt::UserRole + 1).toInt()));
    ui->connectedlabel->setText(QStringLiteral("已选择 %1").arg(m_selectedSsid));
}

void WifiDialog::onStatus(const QString &status)
{
    ui->connectedlabel->setText(status);
}

void WifiDialog::onScanFinished(QList<WifiInfo> networks)
{
    ui->wifiListWidget->clear();
    m_selectedSsid.clear();

    if (networks.isEmpty()) {
        auto *item = new QListWidgetItem(QStringLiteral("未发现热点"));
        item->setFlags(Qt::NoItemFlags);
        ui->wifiListWidget->addItem(item);
    } else {
        for (const WifiInfo &n : networks) {
            auto *item = new QListWidgetItem(ui->wifiListWidget);
            item->setText(QStringLiteral("%1%2    %3")
                              .arg(n.secure ? QStringLiteral("[加密] ")
                                            : QStringLiteral("[开放] "),
                                   n.ssid,
                                   signalText(n.signal)));
            item->setData(Qt::UserRole, n.ssid);
            item->setData(Qt::UserRole + 1, n.signal);
        }
    }

    setBusy(false);
    ui->refreshButton->setText(QStringLiteral("刷新"));
}

void WifiDialog::onConnectFinished(bool ok, const QString &ip)
{
    m_connected = ok;
    setBusy(false);
    ui->connectButton->setText(ok ? QStringLiteral("断开") : QStringLiteral("连接"));

    if (ok) {
        ui->curSsidLabel->setText(m_selectedSsid);
        ui->ipValueLabel->setText(ip);
        ui->signalValueLabel->setText(signalText(-50)); // 已连上，至少是可用强度
        ui->passwordLineEdit->clear();
    }
}

void WifiDialog::onDisconnected()
{
    m_connected = false;
    setBusy(false);
    ui->connectButton->setText(QStringLiteral("连接"));
    ui->curSsidLabel->setText(QStringLiteral("未连接"));
    ui->ipValueLabel->setText(QStringLiteral("-"));
    ui->signalValueLabel->setText(QStringLiteral("-"));
}

void WifiDialog::onCurrentState(const QString &ssid, const QString &ip, int signalDbm)
{
    if (ssid.isEmpty() || ssid == QStringLiteral("-")) {
        m_connected = false;
        ui->curSsidLabel->setText(QStringLiteral("未连接"));
        ui->ipValueLabel->setText(QStringLiteral("-"));
        ui->signalValueLabel->setText(QStringLiteral("-"));
        ui->connectButton->setText(QStringLiteral("连接"));
        ui->connectedlabel->setText(QStringLiteral("未连接，点击「刷新」扫描热点"));
        return;
    }

    m_connected = true;
    m_selectedSsid = ssid;
    ui->curSsidLabel->setText(ssid);
    ui->ipValueLabel->setText(ip.isEmpty() ? QStringLiteral("-") : ip);
    ui->signalValueLabel->setText(signalText(signalDbm));
    ui->connectButton->setText(QStringLiteral("断开"));
    ui->connectedlabel->setText(QStringLiteral("已连接"));
}

QString WifiDialog::signalText(int dbm)
{
    if (dbm >= -50) return QStringLiteral("强");
    if (dbm >= -65) return QStringLiteral("中");
    if (dbm >= -80) return QStringLiteral("弱");
    return QStringLiteral("很弱");
}

void WifiDialog::setBusy(bool busy)
{
    ui->refreshButton->setEnabled(!busy);
    ui->connectButton->setEnabled(!busy);
    ui->wifiListWidget->setEnabled(!busy);
}
