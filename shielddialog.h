#ifndef SHIELDDIALOG_H
#define SHIELDDIALOG_H

#include <QDialog>

class QTimer;
class QLabel;
class QFrame;

namespace Ui {
class ShieldDialog;
}

// ============================================================
//  安防监控 Dialog
//
//  设计要点:
//   - 布防模式三态: 离家布防 / 在家布防 / 撤防 (QRadioButton autoExclusive)
//   - 4 路监控通道 (4 个 checkable 按钮, autoExclusive 实现单选高亮)
//   - 4 个传感器卡片: 正常(绿) / 触发(红)
//   - 报警时红色蒙层 450ms 闪烁 + 状态变红 + 警号鸣响 + 写入报警记录
//
//  Qt5.12(板子) / Qt6(桌面) 双版本兼容:
//   只用两个版本都存在的 API (QRandomGenerator 需 5.10+, 满足)
// ============================================================
class ShieldDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ShieldDialog(QWidget *parent = nullptr);
    ~ShieldDialog();

private slots:
    void on_btnBack_clicked();

    // 4 路监控通道
    void on_btnCh1_clicked();
    void on_btnCh2_clicked();
    void on_btnCh3_clicked();
    void on_btnCh4_clicked();

    // 布防模式
    void on_rbAway_clicked();
    void on_rbHome_clicked();
    void on_rbDisarm_clicked();

    // 快捷操作
    void on_btnSimulate_clicked();
    void on_btnSos_clicked();
    void on_btnDisarmAll_clicked();
    void on_btnClearLog_clicked();

    void onTick();          // 1 秒心跳: 时钟 / 布防计时 / REC 闪烁
    void onBlinkAlarm();    // 450 毫秒: 报警蒙层闪烁

private:
    enum ArmMode { ModeAway = 0, ModeHome = 1, ModeDisarm = 2 };

    void applyMode(ArmMode m, bool logIt);
    void switchChannel(int idx);
    void refreshChannelButtons();
    void refreshVideoLook();
    void refreshStatusView();
    void refreshDeviceStat();

    void setSensor(int idx, bool active, const QString &text);
    void triggerAlarm(const QString &zone, const QString &event, const QString &banner);
    void silenceAlarm(bool logIt);
    void addLog(const QString &zone, const QString &event,
                const QString &result, const QString &color);
    void addLogAt(const QString &time, const QString &zone, const QString &event,
                  const QString &result, const QString &color);

    Ui::ShieldDialog *ui;

    ArmMode m_mode;         // 当前布防模式
    int     m_channel;      // 当前监控通道 0..3
    int     m_armedSec;     // 本模式已持续秒数
    int     m_tickCount;    // 心跳计数 (用于画面动态感)
    bool    m_alarming;     // 是否处于报警中
    bool    m_flashOn;      // 蒙层闪烁相位
    bool    m_recOn;        // REC 指示灯相位
    bool    m_sensorActive[4]{false, false, false, false};

    QTimer *m_timerTick;    // 1s
    QTimer *m_timerFlash;   // 450ms
    QTimer *m_timerReset;   // 报警自动复位 (演示用)

    QFrame *m_sensorCard[4]{};
    QLabel *m_sensorDot[4]{};
    QLabel *m_sensorState[4]{};
};

#endif // SHIELDDIALOG_H
