#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include <QVector>
#include <QWidget>

class QTimer;

// ============================================================
//  伪监控画面控件 (纯 QPainter 自绘, 零图片资源)
//
//  为什么自绘而不是贴图:
//   1. 工程里现成的 jpg 是动漫壁纸, 做监控画面违和;
//   2. STM32MP157 没有 GPU, 贴大图 + QSS 滤镜每帧重绘很慢,
//      而画矩形/线段的开销可以忽略;
//   3. 噪声/扫描线/画面抖动全部可控, "活"的感觉更真。
//
//  绘制顺序 (由远及近):
//     背景渐变 → 后墙 → 门窗道具 → 地面透视网格 → 暗角
//     → 雪花噪声 → 扫描线 → 色调滤镜 → 边框+取景角标
//
//  用法 (在 .ui 里以"提升控件"方式挂进 ShieldDialog):
//     videoView->setChannel(0, "前门");
//     videoView->setScene(0);            // 换一个场景布局
//     videoView->setState(VideoWidget::Disarmed);
// ============================================================
class VideoWidget : public QWidget
{
    Q_OBJECT

public:
    enum State {
        Armed,      // 布防中: 正常画面 + 轻微雪花
        Disarmed,   // 撤防: 亮雪花 + NO SIGNAL
        Alarm       // 报警: 红色调 + 强雪花 + 呼吸红框
    };

    explicit VideoWidget(QWidget *parent = nullptr);
    ~VideoWidget() override;

    void setChannel(int idx, const QString &zoneName);
    void setScene(int scene);       // 0..3, 对应门/窗/车库/客厅四种布局
    void setState(State s);
    void setTitleText(const QString &t);   // 画面中央的大字 (空 = 不显示)

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void buildNoise();              // 预生成几张贴图, 避免每帧造随机数
    void paintScene(QPainter &p, int w, int h);
    void paintProp(QPainter &p, int scene, int w, int h);
    void paintFloor(QPainter &p, int w, int h);
    void paintNoise(QPainter &p, int w, int h);
    void paintScanline(QPainter &p, int w, int h);
    void paintVignette(QPainter &p, int w, int h);
    void paintFrame(QPainter &p, int w, int h);

    QTimer *m_timer;
    QVector<QImage> m_noise;        // 4 张 256x256 雪花贴图轮播
    int m_frame;                    // 帧计数, 驱动动画相位
    int m_channel;                  // 通道号
    int m_scene;                    // 场景布局
    State m_state;
    QString m_zoneName;
    QString m_title;
};

#endif // VIDEOWIDGET_H
