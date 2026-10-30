#include "videowidget.h"

#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPaintEvent>
#include <QRandomGenerator>
#include <QRadialGradient>
#include <QTimer>

// ---- 4 个通道的色调 (夜视绿 / 冷蓝 / 琥珀 / 红外紫) ----
struct Tint { int r, g, b; };
static const Tint kTint[4] = {
    {  40, 255, 130 },   // 通道1 夜视绿
    { 110, 180, 255 },   // 通道2 冷蓝
    { 255, 190,  90 },   // 通道3 琥珀
    { 210, 130, 255 },   // 通道4 红外紫
};

// 单张雪花贴图的边长 (贴图按需平铺到整个画面)
static const int kNoiseTile = 192;
static const int kNoiseCount = 4;

VideoWidget::VideoWidget(QWidget *parent)
    : QWidget(parent)
    , m_timer(nullptr)
    , m_frame(0)
    , m_channel(0)
    , m_scene(0)
    , m_state(Armed)
    , m_zoneName(QStringLiteral("前门"))
{
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setMinimumSize(160, 90);

    buildNoise();

    // 12fps: 板子上没有 GPU, 再快也只是白烧 CPU, 而监控画面本来就该有点迟滞感
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        ++m_frame;
        update();
    });
    m_timer->start(80);
}

VideoWidget::~VideoWidget() = default;

void VideoWidget::setChannel(int idx, const QString &zoneName)
{
    m_channel  = qBound(0, idx, 3);
    m_zoneName = zoneName;
    update();
}

void VideoWidget::setScene(int scene)
{
    m_scene = qBound(0, scene, 3);
    update();
}

void VideoWidget::setState(State s)
{
    m_state = s;
    update();
}

void VideoWidget::setTitleText(const QString &t)
{
    m_title = t;
    update();
}

// ------------------------------------------------------------
//  预生成雪花贴图: 每帧 new 一堆随机数在 ARM 上很浪费,
//  这里造 4 张 192x192 的 ARGB 噪声, 轮播平铺即可乱得看不出来。
// ------------------------------------------------------------
void VideoWidget::buildNoise()
{
    m_noise.clear();
    for (int n = 0; n < kNoiseCount; ++n) {
        QImage img(kNoiseTile, kNoiseTile, QImage::Format_ARGB32);
        img.fill(Qt::transparent);

        // 直接写像素比逐点 QPainter::drawPoint 快一个数量级
        for (int y = 0; y < kNoiseTile; ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < kNoiseTile; ++x) {
                const int a = QRandomGenerator::global()->bounded(70);
                line[x] = qRgba(255, 255, 255, a);
            }
        }
        m_noise.append(img);
    }
}

// ------------------------------------------------------------
void VideoWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter p(this);
    const int w = width();
    const int h = height();

    paintScene(p, w, h);        // 背景 + 墙面 + 道具 + 地面
    paintVignette(p, w, h);     // 暗角 (先压暗, 再叠噪声才像镜头)
    paintNoise(p, w, h);        // 雪花
    paintScanline(p, w, h);     // 扫描线 + 滚动亮带

    // ---- 状态相关的色调叠加 ----
    const Tint &t = kTint[m_channel];
    QColor tint(t.r, t.g, t.b);

    switch (m_state) {
    case Disarmed:
        p.fillRect(rect(), QColor(6, 9, 14, 150));
        break;
    case Alarm: {
        // 报警: 红色染色 + 呼吸强度随帧数变化
        const int pulse = 70 + (m_frame % 20) * 4;
        p.fillRect(rect(), QColor(220, 38, 38, qMin(pulse, 150)));
        break;
    }
    default:
        p.fillRect(rect(), QColor(tint.red(), tint.green(), tint.blue(), 46));
        break;
    }

    paintFrame(p, w, h);        // 白色取景角标 + 红边框

    // ---- 中央大字 (NO SIGNAL / 报警提示) ----
    if (!m_title.isEmpty()) {
        QFont f = p.font();
        f.setPointSize(m_state == Alarm ? 20 : 14);
        f.setBold(true);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 4);
        p.setFont(f);

        if (m_state == Alarm) {
            p.setPen(QColor(255, 255, 255, 200 + (m_frame % 10) * 5));
        } else {
            p.setPen(QColor(120, 140, 160, 220));
        }
        p.drawText(rect(), Qt::AlignCenter, m_title);
    }
}

void VideoWidget::paintScene(QPainter &p, int w, int h)
{
    // ---- 背景: 上暗下更暗的渐变, 模拟夜里的房间 ----
    QLinearGradient bg(0, 0, 0, h);
    bg.setColorAt(0.0, QColor(26, 32, 42));
    bg.setColorAt(0.55, QColor(16, 21, 29));
    bg.setColorAt(1.0, QColor(10, 13, 19));
    p.fillRect(rect(), bg);

    const int horizon = int(h * 0.62);   // 地平线 / 墙脚线

    // ---- 墙面 ----
    p.fillRect(QRect(0, 0, w, horizon), QColor(22, 28, 37));

    // 墙面纹理: 几条垂直的明暗竖纹, 避免大色块看着像纯色底
    p.setPen(QPen(QColor(30, 38, 50), 1));
    for (int x = w / 8; x < w; x += w / 8) {
        p.drawLine(x, 0, x, horizon);
    }

    // ---- 墙脚线 ----
    p.setPen(QPen(QColor(48, 60, 78), 2));
    p.drawLine(0, horizon, w, horizon);

    paintProp(p, m_scene, w, h);        // 门窗道具
    paintFloor(p, w, h);                // 地面透视网格
}

// 每个通道一套不同的"现场道具", 用最少的图形画出可辨识的轮廓
void VideoWidget::paintProp(QPainter &p, int scene, int w, int h)
{
    const int horizon = int(h * 0.62);
    const int cx = w / 2;
    const QColor dark(14, 18, 25);
    const QColor edge(72, 90, 116);      // 轮廓线要够亮, 否则贴在墙色上看不出来
    const QColor lighter(48, 58, 75);    // 道具底色略亮于墙, 形成前后层次

    switch (scene) {
    case 0: {
        // 前门: 一扇带门框的门 + 门把手
        const int dw = int(w * 0.22);
        const int dh = int(h * 0.46);
        const int dx = cx - dw / 2;
        const int dy = horizon - dh;

        p.fillRect(QRect(dx - 5, dy - 5, dw + 10, dh + 5), lighter);
        p.fillRect(QRect(dx, dy, dw, dh), dark);
        p.setPen(QPen(edge, 2));
        p.drawRect(QRect(dx, dy, dw, dh));

        // 门把手
        p.setBrush(QColor(170, 180, 195));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPoint(dx + dw - 14, dy + dh / 2), 4, 4);
        p.setBrush(Qt::NoBrush);
        break;
    }
    case 1: {
        // 后院: 一扇推拉窗 + 一棵树的剪影
        const int ww = int(w * 0.30);
        const int wh = int(h * 0.30);
        const int wx = cx - ww / 2 - int(w * 0.06);
        const int wy = int(h * 0.16);

        p.fillRect(QRect(wx - 5, wy - 5, ww + 10, wh + 10), lighter);
        p.fillRect(QRect(wx, wy, ww, wh), QColor(30, 42, 55));
        p.setPen(QPen(edge, 2));
        p.drawRect(QRect(wx, wy, ww, wh));
        p.drawLine(wx + ww / 2, wy, wx + ww / 2, wy + wh);   // 窗棂

        // 树剪影: 用接近纯黑, 才压得住墙面底色形成轮廓
        const QColor silhouette(6, 9, 13);
        p.setPen(Qt::NoPen);
        p.setBrush(silhouette);
        const int tx = cx + int(w * 0.24);
        p.fillRect(QRect(tx - 4, horizon - int(h * 0.30), 8, int(h * 0.30)), silhouette);
        p.drawEllipse(QPoint(tx, horizon - int(h * 0.34)), int(w * 0.09), int(h * 0.12));
        p.drawEllipse(QPoint(tx - int(w * 0.07), horizon - int(h * 0.28)),
                      int(w * 0.06), int(h * 0.07));
        p.setBrush(Qt::NoBrush);
        break;
    }
    case 2: {
        // 车库: 卷帘门横条纹 + 地上的车轮
        const int dw = int(w * 0.46);
        const int dh = int(h * 0.44);
        const int dx = cx - dw / 2;
        const int dy = horizon - dh;

        p.fillRect(QRect(dx, dy, dw, dh), QColor(20, 25, 33));
        p.setPen(QPen(edge, 2));
        p.drawRect(QRect(dx, dy, dw, dh));
        p.setPen(QPen(QColor(58, 72, 94), 3));
        for (int y = dy + 8; y < dy + dh; y += 12) {         // 卷帘横条
            p.drawLine(dx + 4, y, dx + dw - 4, y);
        }

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(8, 11, 16));
        const int ry = horizon + int(h * 0.10);
        p.drawEllipse(QPoint(cx - int(w * 0.10), ry), int(w * 0.045), int(h * 0.045));
        p.drawEllipse(QPoint(cx + int(w * 0.10), ry), int(w * 0.045), int(h * 0.045));
        p.setBrush(Qt::NoBrush);
        break;
    }
    default: {
        // 客厅: 沙发轮廓 + 对面的电视柜
        const int sy = horizon - int(h * 0.06);
        p.fillRect(QRect(int(w * 0.10), sy - int(h * 0.18),
                         int(w * 0.34), int(h * 0.16)), QColor(24, 30, 40));
        p.setPen(QPen(edge, 2));
        p.drawRect(QRect(int(w * 0.10), sy - int(h * 0.18),
                         int(w * 0.34), int(h * 0.16)));
        p.drawLine(int(w * 0.10), sy - int(h * 0.10),
                   int(w * 0.44), sy - int(h * 0.10));       // 靠背分界

        // 电视
        const int tx = int(w * 0.62);
        const int ty = horizon - int(h * 0.24);
        p.fillRect(QRect(tx, ty, int(w * 0.22), int(h * 0.16)), QColor(8, 11, 16));
        p.setPen(QPen(edge, 2));
        p.drawRect(QRect(tx, ty, int(w * 0.22), int(h * 0.16)));
        break;
    }
    }
}

// 地面透视网格: 从地平线向下发散, 越近间距越大
void VideoWidget::paintFloor(QPainter &p, int w, int h)
{
    const int horizon = int(h * 0.62);
    const int cx = w / 2;

    // 竖向: 都从地平线上的灭点发散出去
    p.setPen(QPen(QColor(34, 44, 58), 1));
    for (int i = -8; i <= 8; ++i) {
        const int xBottom = cx + i * (w / 8);
        p.drawLine(cx, horizon, xBottom, h);
    }

    // 横向: 间距按 1/(i) 递减, 模拟近大远小
    for (int i = 1; i <= 7; ++i) {
        const int y = horizon + int((h - horizon) * (1.0 - 1.0 / (i * 0.9 + 1.0)));
        p.drawLine(0, y, w, y);
    }
}

void VideoWidget::paintVignette(QPainter &p, int w, int h)
{
    // 四角压暗, 中心略亮 —— 镜头暗角, 是"监控味"的关键一笔
    QRadialGradient rg(w / 2.0, h / 2.0, qMax(w, h) * 0.62);
    rg.setColorAt(0.0, QColor(0, 0, 0, 0));
    rg.setColorAt(0.62, QColor(0, 0, 0, 40));
    rg.setColorAt(1.0, QColor(0, 0, 0, 170));
    p.fillRect(rect(), rg);

    // 顶部再压一道, 模拟摄像头安装角度带来的阴影
    QLinearGradient tg(0, 0, 0, h * 0.35);
    tg.setColorAt(0.0, QColor(0, 0, 0, 90));
    tg.setColorAt(1.0, QColor(0, 0, 0, 0));
    p.fillRect(QRect(0, 0, w, int(h * 0.35)), tg);
}

void VideoWidget::paintNoise(QPainter &p, int w, int h)
{
    if (m_noise.isEmpty())
        return;

    // 撤防/报警时雪花更重 —— 信号不好的感觉
    int alpha = 60;
    if (m_state == Disarmed)     alpha = 130;
    else if (m_state == Alarm)   alpha = 170;

    p.setOpacity(alpha / 255.0);
    // 4 张贴图轮流铺, 并让平铺原点每帧漂移, 避免看出重复图案
    const QImage &tile = m_noise[m_frame % m_noise.size()];
    const int ox = -((m_frame * 7) % kNoiseTile);
    const int oy = -((m_frame * 11) % kNoiseTile);
    for (int y = oy; y < h; y += kNoiseTile) {
        for (int x = ox; x < w; x += kNoiseTile) {
            p.drawImage(x, y, tile);
        }
    }
    p.setOpacity(1.0);
}

void VideoWidget::paintScanline(QPainter &p, int w, int h)
{
    // 固定扫描线
    p.setPen(QPen(QColor(0, 0, 0, 40), 1));
    for (int y = 0; y < h; y += 3) {
        p.drawLine(0, y, w, y);
    }

    // 一条缓慢下移的亮带 (CRT 刷新感), 4px 高
    const int band = (m_frame * 3) % (h + 40) - 20;
    QLinearGradient bg(0, band, 0, band + 40);
    bg.setColorAt(0.0, QColor(255, 255, 255, 0));
    bg.setColorAt(0.5, QColor(255, 255, 255, 22));
    bg.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.fillRect(QRect(0, band, w, 40), bg);
}

void VideoWidget::paintFrame(QPainter &p, int w, int h)
{
    // 取景角标 (四角的 L 形白线)
    const int len = 16;
    p.setPen(QPen(QColor(255, 255, 255, 90), 2));
    p.drawLine(6, 6, 6 + len, 6);            p.drawLine(6, 6, 6, 6 + len);
    p.drawLine(w - 7, 6, w - 7 - len, 6);    p.drawLine(w - 7, 6, w - 7, 6 + len);
    p.drawLine(6, h - 7, 6 + len, h - 7);    p.drawLine(6, h - 7, 6, h - 7 - len);
    p.drawLine(w - 7, h - 7, w - 7 - len, h - 7);
    p.drawLine(w - 7, h - 7, w - 7, h - 7 - len);

    // 报警时边框变红并呼吸
    if (m_state == Alarm) {
        const int a = 120 + (m_frame % 16) * 8;
        p.setPen(QPen(QColor(239, 68, 68, qMin(a, 255)), 3));
        p.drawRect(rect().adjusted(1, 1, -2, -2));
    } else {
        p.setPen(QPen(QColor(40, 52, 68), 2));
        p.drawRect(rect().adjusted(1, 1, -2, -2));
    }
}
