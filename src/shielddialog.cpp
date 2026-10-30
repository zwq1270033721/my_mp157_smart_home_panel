#include "shielddialog.h"
#include "ui_ShieldDialog.h"
#include "videowidget.h"

#include <QDateTime>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QRandomGenerator>
#include <QTableWidgetItem>
#include <QTime>
#include <QTimer>

// ============================================================
//  静态数据表
// ============================================================

// 4 路监控通道
static const char *const kZoneName[4] = { "前门", "后院", "车库", "客厅" };
static const char *const kCamNo[4]    = { "CAM-01", "CAM-02", "CAM-03", "CAM-04" };

// 4 个传感器: 前门门磁 / 客厅红外 / 厨房烟雾 / 卫生间水浸
static const char *const kSensorZone[4]  = { "前门", "客厅", "厨房", "卫生间" };
static const char *const kSensorEvent[4] = { "门磁被打开", "检测到人体移动",
                                             "烟雾浓度超限", "检测到漏水" };
// 在家布防时, 室内传感器全部旁路 (只剩前门门磁工作), 避免自己人走动误报
static const bool kBypassInHome[4] = { false, true, true, true };

// ============================================================
//  样式常量
// ============================================================
static const char *const kStatusOnStyle   = "font-size:20pt;font-weight:bold;color:#22c55e;";
static const char *const kStatusOffStyle  = "font-size:20pt;font-weight:bold;color:#f59e0b;";
static const char *const kStatusAlarmStyle= "font-size:20pt;font-weight:bold;color:#ef4444;";
static const char *const kValGreen        = "font-size:10pt;color:#22c55e;";
static const char *const kValRed          = "font-size:10pt;color:#ef4444;";
static const char *const kValGray         = "font-size:10pt;color:#8fa3b8;";

// 画面叠加角标 (半透明黑底胶囊)
static const char *const kBadgeStyle =
    "background-color:rgba(0,0,0,150);color:%1;border-radius:13px;font-size:%2;";

// 小工具: 秒 -> "HH:mm:ss"
static QString fmtDuration(int sec)
{
    if (sec < 0) sec = 0;
    return QString("%1:%2:%3")
            .arg(sec / 3600, 2, 10, QLatin1Char('0'))
            .arg((sec / 60) % 60, 2, 10, QLatin1Char('0'))
            .arg(sec % 60, 2, 10, QLatin1Char('0'));
}

// 小工具: 给一个 QLabel 换文字 + 颜色 (不动 alignment, 那是 widget 属性不是样式)
static void setStat(QLabel *l, const QString &text, const char *style)
{
    l->setText(text);
    l->setStyleSheet(QLatin1String(style));
}

// ============================================================
//  构造 / 析构
// ============================================================
ShieldDialog::ShieldDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ShieldDialog)
    , m_mode(ModeAway)
    , m_channel(0)
    , m_armedSec(0)
    , m_tickCount(0)
    , m_alarming(false)
    , m_flashOn(false)
    , m_recOn(true)
{
    ui->setupUi(this);

    // ---- 传感器控件指针装箱, 方便按下标批量操作 ----
    m_sensorCard[0]  = ui->cardSensor1;  m_sensorCard[1]  = ui->cardSensor2;
    m_sensorCard[2]  = ui->cardSensor3;  m_sensorCard[3]  = ui->cardSensor4;
    m_sensorDot[0]   = ui->dotSensor1;   m_sensorDot[1]   = ui->dotSensor2;
    m_sensorDot[2]   = ui->dotSensor3;   m_sensorDot[3]   = ui->dotSensor4;
    m_sensorState[0] = ui->stateSensor1; m_sensorState[1] = ui->stateSensor2;
    m_sensorState[2] = ui->stateSensor3; m_sensorState[3] = ui->stateSensor4;

    // ---- 报警记录表: 表头 / 列宽 / 行高 ----
    QHeaderView *hh = ui->tableLog->horizontalHeader();
    hh->setSectionResizeMode(0, QHeaderView::Fixed);   hh->resizeSection(0, 78);
    hh->setSectionResizeMode(1, QHeaderView::Fixed);   hh->resizeSection(1, 72);
    hh->setSectionResizeMode(2, QHeaderView::Stretch);
    hh->setSectionResizeMode(3, QHeaderView::Fixed);   hh->resizeSection(3, 76);
    ui->tableLog->verticalHeader()->setVisible(false);
    ui->tableLog->verticalHeader()->setDefaultSectionSize(26);
    ui->tableLog->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableLog->setSelectionMode(QAbstractItemView::NoSelection);
    ui->tableLog->setFocusPolicy(Qt::NoFocus);
    ui->tableLog->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    ui->btnSimulate->setToolTip("随机触发一个布防中的传感器, 用于演示报警流程");
    ui->btnSos->setToolTip("手动紧急报警, 不受布防模式限制");
    ui->btnDisarmAll->setToolTip("解除报警并把系统切到撤防");

    // ---- 预置 3 条历史记录, 让页面一打开就有内容 ----
    // 放在代码里而不是 .ui 里: QTableWidget 的 setItem() 对不存在的行会静默丢弃,
    // 而 uic 生成 .ui 的预填 item 时不会先补 rowCount(), 结果运行时段错误。
    struct SeedLog { const char *time, *zone, *event, *result, *color; };
    static const SeedLog kSeed[3] = {
        { "08:12:41", "前门", "门磁被打开",   "已处理", "#22c55e" },
        { "07:45:03", "客厅", "移动侦测",     "已处理", "#22c55e" },
        { "07:20:18", "系统", "离家布防已开启", "正常",   "#22c55e" },
    };
    // 倒着插, 因为 addLogAt 每次都是插到第 0 行 (最新的在最上面)
    for (int i = 2; i >= 0; --i) {
        addLogAt(QString::fromLatin1(kSeed[i].time),
                 QString::fromUtf8(kSeed[i].zone),
                 QString::fromUtf8(kSeed[i].event),
                 QString::fromUtf8(kSeed[i].result),
                 QString::fromLatin1(kSeed[i].color));
    }

    // ---- 定时器 ----
    m_timerTick = new QTimer(this);
    connect(m_timerTick, &QTimer::timeout, this, &ShieldDialog::onTick);
    m_timerTick->start(1000);

    m_timerFlash = new QTimer(this);
    connect(m_timerFlash, &QTimer::timeout, this, &ShieldDialog::onBlinkAlarm);

    // 演示用: 报警 8 秒后自动复位, 免得画面一直闪。
    // 想让它一直响就把下面两行注释掉, 改为只靠"一键撤防"解除。
    m_timerReset = new QTimer(this);
    m_timerReset->setSingleShot(true);
    connect(m_timerReset, &QTimer::timeout, this, [this]() { silenceAlarm(true); });

    // ---- 初始状态: 离家布防 (不写日志, 但要刷出时钟/画面) ----
    onTick();
    applyMode(ModeAway, false);
}

ShieldDialog::~ShieldDialog()
{
    delete ui;
}

void ShieldDialog::on_btnBack_clicked()
{
    this->close();
}

// ============================================================
//  UI 刷新
// ============================================================

// 顶部时钟 + 布防计时 + REC 指示灯
void ShieldDialog::onTick()
{
    const QDateTime now = QDateTime::currentDateTime();
    ui->lblClock->setText(now.toString("yyyy/MM/dd  HH:mm:ss"));
    ui->lblVideoTime->setText(now.toString("HH:mm:ss"));

    // 布防时长 (撤防状态不计时)
    if (m_mode != ModeDisarm) {
        m_armedSec++;
        if (!m_alarming) {
            ui->lblArmDetail->setText(
                QString("%1 · %2")
                    .arg(m_mode == ModeAway ? QStringLiteral("离家布防")
                                            : QStringLiteral("在家布防"),
                         fmtDuration(m_armedSec)));
        }
    }

    // REC 指示灯闪烁
    m_recOn = !m_recOn;
    if (m_mode != ModeDisarm) {
        ui->lblLive->setText(m_recOn ? QStringLiteral("● REC") : QStringLiteral("○ REC"));
    }

    // 画面自身的动态 (雪花/扫描线/抖动) 由 VideoWidget 内部 12fps 定时器负责,
    // 这里只推进全局心跳计数, 供布防时长等逻辑使用
    m_tickCount++;
}

void ShieldDialog::onBlinkAlarm()
{
    if (!m_alarming) {
        m_timerFlash->stop();
        ui->lblAlarm->setVisible(false);
        return;
    }
    m_flashOn = !m_flashOn;
    ui->lblAlarm->setVisible(m_flashOn);
}

// 布防状态标签
void ShieldDialog::refreshStatusView()
{
    if (m_alarming) {
        setStat(ui->lblArmStatus, QStringLiteral("报警中"), kStatusAlarmStyle);
        ui->lblArmDetail->setText(QStringLiteral("请确认现场情况"));
        return;
    }

    switch (m_mode) {
    case ModeAway:
        setStat(ui->lblArmStatus, QStringLiteral("已布防"), kStatusOnStyle);
        ui->lblArmDetail->setText(QString("离家布防 · %1").arg(fmtDuration(m_armedSec)));
        break;
    case ModeHome:
        setStat(ui->lblArmStatus, QStringLiteral("在家布防"), kStatusOnStyle);
        ui->lblArmDetail->setText(QString("在家布防 · %1").arg(fmtDuration(m_armedSec)));
        break;
    default:
        setStat(ui->lblArmStatus, QStringLiteral("已撤防"), kStatusOffStyle);
        ui->lblArmDetail->setText(QStringLiteral("系统处于撤防状态"));
        break;
    }
}

// 监控画面: 状态 + 通道交给 VideoWidget 自绘, 这里只决定"该显示什么"
void ShieldDialog::refreshVideoLook()
{
    const bool off = (m_mode == ModeDisarm) && !m_alarming;

    // 场景跟随通道走, 每个通道一套不同的现场布局
    ui->videoView->setScene(m_channel);
    ui->videoView->setChannel(m_channel,
                              QString::fromUtf8(kZoneName[m_channel]));

    if (m_alarming) {
        ui->videoView->setState(VideoWidget::Alarm);
        ui->videoView->setTitleText(QString());
    } else if (off) {
        ui->videoView->setState(VideoWidget::Disarmed);
        ui->videoView->setTitleText(QStringLiteral("N O   S I G N A L"));
    } else {
        ui->videoView->setState(VideoWidget::Armed);
        ui->videoView->setTitleText(QString());
    }

    // 角标: 撤防时藏起 REC, 报警时把 REC 换成红色告警
    ui->lblLive->setVisible(!off);
    ui->lblVideoTime->setVisible(!off);
    ui->lblLive->setStyleSheet(
        QString(QLatin1String(kBadgeStyle))
            .arg(m_alarming ? "#ef4444" : "#22c55e").arg("11pt"));
    ui->lblVideoTime->setStyleSheet(
        QString(QLatin1String(kBadgeStyle)).arg("#cbd5e1").arg("10pt"));
}

// 切换监控通道 (idx 0..3)
void ShieldDialog::switchChannel(int idx)
{
    if (idx < 0 || idx > 3) idx = 0;
    m_channel = idx;

    ui->lblChannel->setText(QString("%1 · %2")
                                .arg(QLatin1String(kCamNo[idx]),
                                     QString::fromUtf8(kZoneName[idx])));

    // 代码调用时也要保证按钮高亮状态同步 (autoExclusive 只管用户点击)
    QPushButton *btns[4] = { ui->btnCh1, ui->btnCh2, ui->btnCh3, ui->btnCh4 };
    for (int i = 0; i < 4; ++i) {
        btns[i]->setChecked(i == idx);
    }

    refreshVideoLook();
}

// 各布防模式下各通道的启用情况
void ShieldDialog::refreshChannelButtons()
{
    bool on[4] = { true, true, true, true };
    if (m_mode == ModeDisarm) {
        on[0] = on[1] = on[2] = on[3] = false;   // 系统关闭 -> 所有通道停机
    } else if (m_mode == ModeHome) {
        on[3] = false;                           // 在家布防 -> 室内通道旁路
    }

    QPushButton *btns[4] = { ui->btnCh1, ui->btnCh2, ui->btnCh3, ui->btnCh4 };
    for (int i = 0; i < 4; ++i) {
        btns[i]->setEnabled(on[i]);
        if (on[i]) {
            btns[i]->setToolTip(QString("查看 %1 实时画面")
                                    .arg(QString::fromUtf8(kZoneName[i])));
        } else if (m_mode == ModeDisarm) {
            btns[i]->setToolTip(QStringLiteral("系统已撤防, 监控通道关闭"));
        } else {
            btns[i]->setToolTip(QStringLiteral("在家布防: 室内通道已旁路"));
        }
    }

    switchChannel(on[m_channel] ? m_channel : 0);
}

// 系统状态栏 (对应传感器是否有离线)
void ShieldDialog::refreshDeviceStat()
{
    int offDoor = 0;    // 门磁
    int offDet  = 0;    // 探测器
    if (m_sensorActive[0]) offDoor++;
    if (m_sensorActive[1]) offDet++;
    if (m_sensorActive[2]) offDet++;
    if (m_sensorActive[3]) offDet++;

    setStat(ui->lblDev1Val, QStringLiteral("4 / 4 在线"), kValGreen);

    setStat(ui->lblDev2Val,
            QString("%1 / 8 在线").arg(8 - offDoor),
            offDoor > 0 ? kValRed : kValGreen);

    setStat(ui->lblDev3Val,
            QString("%1 / 6 在线").arg(6 - offDet),
            offDet > 0 ? kValRed : kValGreen);

    if (m_alarming) {
        setStat(ui->lblDev4Val, QStringLiteral("鸣响中"), kValRed);
    } else if (m_mode == ModeDisarm) {
        setStat(ui->lblDev4Val, QStringLiteral("待机"), kValGray);
    } else {
        setStat(ui->lblDev4Val, QStringLiteral("正常"), kValGreen);
    }
}

// ============================================================
//  布防模式
// ============================================================
void ShieldDialog::applyMode(ArmMode m, bool logIt)
{
    m_mode = m;
    m_armedSec = 0;

    // 切换布防模式等同于对当前报警做一次确认
    silenceAlarm(true);

    // 重新布防/撤防都把传感器恢复成正常态
    for (int i = 0; i < 4; ++i) {
        m_sensorActive[i] = false;
        setSensor(i, false, QStringLiteral("正常"));
    }

    // 同步单选按钮 (setChecked 只发 toggled, 不会触发 clicked, 不会递归)
    ui->rbAway->setChecked(m == ModeAway);
    ui->rbHome->setChecked(m == ModeHome);
    ui->rbDisarm->setChecked(m == ModeDisarm);

    refreshChannelButtons();
    refreshStatusView();
    refreshDeviceStat();

    if (logIt) {
        switch (m) {
        case ModeAway:
            addLog(QStringLiteral("系统"), QStringLiteral("离家布防已开启"),
                   QStringLiteral("正常"), "#22c55e");
            break;
        case ModeHome:
            addLog(QStringLiteral("系统"), QStringLiteral("在家布防已开启(室内旁路)"),
                   QStringLiteral("正常"), "#22c55e");
            break;
        default:
            addLog(QStringLiteral("系统"), QStringLiteral("系统已撤防"),
                   QStringLiteral("正常"), "#8fa3b8");
            break;
        }
    }
}

void ShieldDialog::on_rbAway_clicked()
{
    applyMode(ModeAway, true);
}

void ShieldDialog::on_rbHome_clicked()
{
    applyMode(ModeHome, true);
}

void ShieldDialog::on_rbDisarm_clicked()
{
    applyMode(ModeDisarm, true);
}

void ShieldDialog::on_btnDisarmAll_clicked()
{
    applyMode(ModeDisarm, true);
}

// ============================================================
//  监控通道
// ============================================================
void ShieldDialog::on_btnCh1_clicked() { switchChannel(0); }
void ShieldDialog::on_btnCh2_clicked() { switchChannel(1); }
void ShieldDialog::on_btnCh3_clicked() { switchChannel(2); }
void ShieldDialog::on_btnCh4_clicked() { switchChannel(3); }

// ============================================================
//  传感器
// ============================================================
void ShieldDialog::setSensor(int idx, bool active, const QString &text)
{
    if (idx < 0 || idx > 3) return;

    m_sensorActive[idx] = active;

    m_sensorState[idx]->setText(text);
    m_sensorState[idx]->setStyleSheet(active
        ? "font-size:12pt;font-weight:bold;color:#ef4444;"
        : "font-size:12pt;font-weight:bold;color:#22c55e;");

    m_sensorDot[idx]->setStyleSheet(active
        ? "background-color:#ef4444;border-radius:5px;"
        : "background-color:#22c55e;border-radius:5px;");

    m_sensorCard[idx]->setStyleSheet(
        QString("QFrame#%1{background-color:%2;border:1px solid %3;border-radius:12px;}")
            .arg(m_sensorCard[idx]->objectName(),
                 active ? "#2a1416" : "#1a2332",
                 active ? "#ef4444" : "#2b3a4d"));
}

// ============================================================
//  报警
// ============================================================
void ShieldDialog::triggerAlarm(const QString &zone, const QString &event,
                                const QString &banner)
{
    m_alarming = true;
    m_flashOn  = true;

    ui->lblAlarm->setText(banner);
    ui->lblAlarm->setVisible(true);
    ui->lblAlarm->raise();          // 压在画面/角标之上
    m_timerFlash->start(450);

    refreshStatusView();
    refreshDeviceStat();
    refreshVideoLook();             // 画面切红色告警
    addLog(zone, event, QStringLiteral("未处理"), "#ef4444");

    m_timerReset->start(8000);      // 演示用自动复位, 见构造函数里的说明
}

void ShieldDialog::silenceAlarm(bool logIt)
{
    if (!m_alarming) return;

    m_alarming = false;
    m_timerFlash->stop();
    m_timerReset->stop();
    ui->lblAlarm->setVisible(false);

    if (logIt) {
        addLog(QStringLiteral("系统"), QStringLiteral("报警已解除"),
               QStringLiteral("已处理"), "#22c55e");
    }

    refreshStatusView();
    refreshDeviceStat();
    refreshVideoLook();             // 画面恢复正常
}

void ShieldDialog::on_btnSimulate_clicked()
{
    if (m_mode == ModeDisarm) {
        addLog(QStringLiteral("系统"), QStringLiteral("系统已撤防, 传感器不触发"),
               QStringLiteral("忽略"), "#8fa3b8");
        return;
    }
    if (m_alarming) {
        addLog(QStringLiteral("系统"), QStringLiteral("报警未解除, 忽略本次触发"),
               QStringLiteral("忽略"), "#f59e0b");
        return;
    }

    // 收集本次可触发的传感器: 跳过在家布防时被旁路的室内传感器
    int cand[4];
    int n = 0;
    for (int i = 0; i < 4; ++i) {
        if (m_mode == ModeHome && kBypassInHome[i]) continue;
        if (m_sensorActive[i]) continue;        // 已触发的先跳过, 保证每次都有变化
        cand[n++] = i;
    }
    if (n == 0) {                               // 全触发过了, 那就允许重复触发
        for (int i = 0; i < 4; ++i) {
            if (m_mode == ModeHome && kBypassInHome[i]) continue;
            cand[n++] = i;
        }
    }
    if (n == 0) {
        addLog(QStringLiteral("系统"), QStringLiteral("无可用传感器"),
               QStringLiteral("忽略"), "#8fa3b8");
        return;
    }

    const int idx = cand[QRandomGenerator::global()->bounded(n)];
    const QString zone  = QString::fromUtf8(kSensorZone[idx]);
    const QString event = QString::fromUtf8(kSensorEvent[idx]);

    setSensor(idx, true, QStringLiteral("触发"));
    refreshDeviceStat();

    // 画面自动切到对应通道 (前门 -> CH1, 客厅 -> CH4), 通道被旁路时切到默认通道
    int ch = m_channel;
    if (idx == 0) {
        ch = 0;
    } else if (idx == 1 && m_mode != ModeHome) {
        ch = 3;
    }
    switchChannel(ch);

    triggerAlarm(zone, event, QString("⚠   %1 报警").arg(zone));
}

void ShieldDialog::on_btnSos_clicked()
{
    if (m_alarming) {                    // 已经在响, 再来一次就当延长
        m_timerReset->start(8000);
        return;
    }
    triggerAlarm(QStringLiteral("紧急"), QStringLiteral("手动触发紧急报警"),
                 QStringLiteral("⚠   紧 急 报 警"));
}

// ============================================================
//  报警记录
// ============================================================
void ShieldDialog::addLog(const QString &zone, const QString &event,
                          const QString &result, const QString &color)
{
    addLogAt(QTime::currentTime().toString("HH:mm:ss"), zone, event, result, color);
}

// 带显式时间的版本: 构造函数播种历史记录时用, 免得写完再回头改写时间格
void ShieldDialog::addLogAt(const QString &time, const QString &zone,
                            const QString &event, const QString &result,
                            const QString &color)
{
    ui->tableLog->insertRow(0);

    QTableWidgetItem *items[4];
    items[0] = new QTableWidgetItem(time);
    items[1] = new QTableWidgetItem(zone);
    items[2] = new QTableWidgetItem(event);
    items[3] = new QTableWidgetItem(result);

    for (int i = 0; i < 4; ++i) {
        items[i]->setForeground(QColor(i == 3 ? color : QStringLiteral("#cbd5e1")));
        items[i]->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        ui->tableLog->setItem(0, i, items[i]);
    }

    // 只留最近 60 条, 触屏设备上无限增长会越来越卡
    while (ui->tableLog->rowCount() > 60) {
        ui->tableLog->removeRow(ui->tableLog->rowCount() - 1);
    }
}

void ShieldDialog::on_btnClearLog_clicked()
{
    ui->tableLog->setRowCount(0);
    addLog(QStringLiteral("系统"), QStringLiteral("记录已清空"),
           QStringLiteral("正常"), "#8fa3b8");
}
