// ============================================================================
//  愤怒的小鸟 (Angry Birds)  for  ATK-DLMP157 / 1024x600
//
//  纯 QPainter 绘制 + 自研冲量法 2D 刚体物理 + 实时 PCM 音效合成
//  无任何外部资源文件依赖，Qt5.12 (板子) / Qt6.4 (桌面) 双兼容
// ============================================================================

#include "game.h"
#include "ui_game.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QPolygonF>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFont>
#include <QFontMetrics>
#include <QScreen>
#include <QGuiApplication>
#include <QResizeEvent>
#include <QIODevice>
#include <QAudioFormat>
// Qt6 把音频设备 API 换了一套：
//   Qt5.12: QAudioDeviceInfo::defaultOutputDevice() + new QAudioOutput(dev, fmt)
//   Qt6   : QMediaDevices::defaultAudioOutput() + new QAudioSink(dev, fmt)
// QAudioOutput 这个名字在 Qt6 里被 typedef 成 QAudioSink，所以类型名不用改。
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#  include <QAudioDevice>
#  include <QMediaDevices>
#  include <QAudioSink>
#else
#  include <QAudioDeviceInfo>
#  include <QAudioOutput>
#endif
#include <QtMath>
#include <QDebug>

#include <cmath>

// ============================================================================
//  第一部分：实时 PCM 音效合成器
//  板子/桌面都不依赖任何音频文件，直接用 QIODevice 喂正弦/噪声给声卡
// ============================================================================
namespace {
const int   kRate  = 22050;      // 采样率
const qreal kPI    = 3.14159265358979323846;
}

// ---- 帧率与物理子步 --------------------------------------------------------
// 渲染 40fps。物理不能直接用 1/40 的步长: 鸟最快约 1600px/s, 一步就是 40px,
// 比积木还厚, 会直接穿模。所以每帧内部再切 2 个子步 (等效 80Hz) 跑物理。
namespace {
const int   kFrameMs   = 25;                    // 1000/40
const qreal kFrameDt   = kFrameMs / 1000.0;     // 0.025
const int   kSubSteps  = 2;                     // 物理子步数
}

class GameAudio : public QIODevice
{
    // 注意: Voice 必须定义在 voiceSample() 之前 —— 成员函数的"函数体"虽然
    // 处在 complete-class 上下文里, 但"形参列表"不是, 引用后置声明的类型会报错
    enum { kVoices = 8 };
    struct Voice { bool active = false; int type = 0; double t = 0.0; int seed = 0; };

    Voice  m_voice[kVoices];
    quint32 m_rng      = 0x12345678u;
    qint64 m_musicPos  = 0;
    bool   m_musicOn   = true;

public:
    explicit GameAudio(QObject *parent = nullptr) : QIODevice(parent)
    {
        for (int i = 0; i < kVoices; ++i) m_voice[i].active = false;
        m_rng = 0x12345678u;
    }

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return 8192 + QIODevice::bytesAvailable(); }
    qint64 writeData(const char *, qint64) override { return 0; }

    void trigger(int type)
    {
        // 找一个空闲/最旧的声部
        int slot = -1; double oldest = -1;
        for (int i = 0; i < kVoices; ++i) {
            if (!m_voice[i].active) { slot = i; break; }
            if (m_voice[i].t > oldest) { oldest = m_voice[i].t; slot = i; }
        }
        if (slot < 0) slot = 0;
        m_voice[slot].active = true;
        m_voice[slot].t      = 0.0;
        m_voice[slot].type   = type;
        // 每次触发给一点随机化，避免连击听起来一模一样
        m_voice[slot].seed   = (int)(nextRand() * 1000.0f);
    }

    void setMusicOn(bool on) { m_musicOn = on; }
    bool musicOn() const     { return m_musicOn; }

protected:
    qint64 readData(char *data, qint64 maxlen) override
    {
        const int frames = (int)(maxlen / 2);          // 16bit 单声道
        qint16 *out = reinterpret_cast<qint16 *>(data);
        for (int i = 0; i < frames; ++i) {
            float s = 0.0f;
            for (int v = 0; v < kVoices; ++v) s += voiceSample(m_voice[v]);
            s += musicSample();
            if (s >  1.0f) s =  1.0f;
            if (s < -1.0f) s = -1.0f;
            out[i] = (qint16)(s * 30000.0f);
            m_musicPos++;
        }
        return (qint64)frames * 2;
    }

private:
    float nextRand()
    {
        // xorshift32，比 rand() 快且无需加锁
        m_rng ^= m_rng << 13; m_rng ^= m_rng >> 17; m_rng ^= m_rng << 5;
        return (float)(m_rng & 0xFFFFFF) / (float)0xFFFFFF;
    }

    // ---- 单个声部采样 -------------------------------------------------------
    float voiceSample(Voice &v)
    {
        if (!v.active) return 0.0f;
        const double t = v.t;
        double d = 0.0, s = 0.0;

        switch (v.type) {
        case 0: {   // 发射：上滑音，土气又带劲
            d = 0.26;
            double f = 780.0 - 620.0 * (t / d);
            double env = 1.0 - t / d;
            s = std::sin(2.0 * kPI * f * t) * env * env * 0.34;
            break;
        }
        case 1: {   // 木箱/积木撞击：短促噪声
            d = 0.13;
            double env = std::pow(1.0 - t / d, 3.0);
            s = ((double)nextRand() * 2.0 - 1.0) * env * 0.34;
            break;
        }
        case 2: {   // 猪被干掉：下滑 "oink"
            d = 0.34;
            double f = 520.0 - 340.0 * (t / d);
            double env = std::pow(1.0 - t / d, 1.5);
            double vib = 1.0 + 0.08 * std::sin(2.0 * kPI * 28.0 * t);
            s = std::sin(2.0 * kPI * f * vib * t) * env * 0.30;
            break;
        }
        case 3: {   // 玻璃碎裂：高频噪声 + 快速衰减
            d = 0.42;
            double env = std::pow(1.0 - t / d, 2.2);
            double hiss = ((double)nextRand() * 2.0 - 1.0);
            double ring = std::sin(2.0 * kPI * 2600.0 * t) * 0.4
                        + std::sin(2.0 * kPI * 3700.0 * t) * 0.25;
            s = (hiss * 0.55 + ring) * env * 0.26;
            break;
        }
        case 4: {   // 爆炸：低频轰鸣 + 噪声
            d = 0.62;
            double env = std::pow(1.0 - t / d, 1.4);
            double boom = std::sin(2.0 * kPI * (90.0 - 55.0 * (t / d)) * t);
            double nois = ((double)nextRand() * 2.0 - 1.0);
            s = (boom * 0.55 + nois * 0.45) * env * 0.42;
            break;
        }
        case 5: {   // 过关：上行琶音 C-E-G-C
            d = 0.72;
            const double f[4] = { 523.25, 659.25, 783.99, 1046.50 };
            int k = (int)(t / (d / 4.0)); if (k > 3) k = 3;
            double tt = t - k * (d / 4.0);
            double env = std::pow(1.0 - tt / (d / 4.0), 0.7) * (1.0 - t / d * 0.35);
            double ph = std::fmod(tt * f[k], 1.0);
            s = (4.0 * std::fabs(ph - 0.5) - 1.0) * env * 0.20;
            break;
        }
        case 6: {   // 失败：下行长音
            d = 0.95;
            double f = 330.0 - 130.0 * (t / d);
            double env = std::pow(1.0 - t / d, 1.1);
            double ph = std::fmod(t * f, 1.0);
            s = (4.0 * std::fabs(ph - 0.5) - 1.0) * env * 0.20;
            break;
        }
        default: v.active = false; return 0.0f;
        }

        v.t += 1.0 / kRate;
        if (t >= d) v.active = false;
        return (float)s;
    }

    // ---- 背景音乐：C 大调轻快循环 -------------------------------------------
    float musicSample()
    {
        if (!m_musicOn) return 0.0f;
        static const float NOTE[8] = { 0.0f, 261.63f, 293.66f, 329.63f, 349.23f,
                                       392.00f, 440.00f, 493.88f };
        static const unsigned char MELODY[64] = {
            5,5,6,7, 7,6,5,4, 3,3,4,5, 5,4,4,0,
            5,5,6,7, 7,6,5,4, 3,3,4,5, 6,6,5,5,
            4,4,5,6, 6,5,4,3, 4,5,6,7, 7,6,6,0,
            5,5,6,7, 7,6,5,4, 3,3,4,5, 6,4,5,0
        };
        static const unsigned char BASS[64] = {
            2,0,2,0, 2,0,2,0, 1,0,1,0, 1,0,1,0,
            2,0,2,0, 2,0,2,0, 1,0,1,0, 2,0,2,0,
            1,0,1,0, 1,0,1,0, 2,0,2,0, 2,0,2,0,
            2,0,2,0, 2,0,2,0, 1,0,1,0, 1,0,1,0
        };

        const double stepDur = 0.215;                       // 八分音符
        const double total   = stepDur * 64.0;
        double time  = m_musicPos / (double)kRate;
        double loopT = std::fmod(time, total);
        int    step  = (int)(loopT / stepDur);
        double inStep = loopT - step * stepDur;
        if (step < 0 || step > 63) return 0.0f;

        float s = 0.0f;
        int n = MELODY[step];
        if (n) {
            double f   = NOTE[n];
            double d   = stepDur * 0.88;
            double env = (inStep > d) ? 0.0 : std::pow(1.0 - inStep / d, 0.55);
            double ph  = std::fmod(inStep * f, 1.0);
            s += (float)((4.0 * std::fabs(ph - 0.5) - 1.0) * env * 0.062);
        }
        int b = BASS[step];
        if (b) {
            double f   = NOTE[b] * 0.5;
            double d   = stepDur * 0.70;
            double env = (inStep > d) ? 0.0 : std::pow(1.0 - inStep / d, 0.8);
            double ph  = std::fmod(inStep * f, 1.0);
            s += (float)((4.0 * std::fabs(ph - 0.5) - 1.0) * env * 0.055);
        }
        return s;
    }
};

// ============================================================================
//  第二部分：几何 / 物理 工具
// ============================================================================
namespace {

inline qreal clampv(qreal v, qreal lo, qreal hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

inline QPointF rotv(const QPointF &v, qreal a)
{
    const qreal c = std::cos(a), s = std::sin(a);
    return QPointF(v.x() * c - v.y() * s, v.x() * s + v.y() * c);
}

inline qreal cross2(const QPointF &a, const QPointF &b)
{
    return a.x() * b.y() - a.y() * b.x();
}

inline QPointF crossSV(qreal w, const QPointF &r)
{
    return QPointF(-w * r.y(), w * r.x());
}

// 欧氏长度。Qt 的 manhattanLength() 在 45° 方向会把距离高估 41%,
// 用它做"拉了多远"的判定会让玩家永远拉不满力度。
inline qreal euclidLen(const QPointF &v)
{
    return std::sqrt(v.x() * v.x() + v.y() * v.y());
}

inline QPointF unit(const QPointF &v)
{
    const qreal l = std::sqrt(v.x() * v.x() + v.y() * v.y());
    return l > 1e-9 ? v / l : QPointF(0, 0);
}

// 一个参与碰撞求解的物体引用（鸟/猪/积木都能用）
struct BodyRef {
    QPointF *pos = nullptr;
    QPointF *vel = nullptr;
    qreal   *angVel = nullptr;
    qreal    invMass = 0.0;
    qreal    invI = 0.0;
    bool    *sleeping = nullptr;
    qreal    radius = 0.0;      // >0 表示圆形
};

// 通用冲量接触求解，nrm 必须由 A 指向 B
//
// outApproach 回传"求解前"的法向接近速度(负值 = 正在靠近)。
// 这一点很要紧: 持续接触(比如猪安静地趴在木梁上)每帧也会产生几十的
// 冲量, 那是重力支撑力, 不是撞击。用它算伤害会让猪开局自己扣血扣死。
// 判断"撞击"只能用接近速度。
qreal solveContact(const BodyRef &A, const BodyRef &B,
                   qreal pen, const QPointF &nrm, const QPointF &contact,
                   qreal e, qreal mu, qreal *outApproach = nullptr)
{
    // --- 位置修正（Baumgarte 比例）---
    // 睡眠中的物体不参与位置修正: 否则上面趴着的猪每帧都会把它顶开一点点,
    // 累积下来整座塔会自己散架。
    // 睡眠体一律按"静态/质量无穷"处理 —— 位置修正和冲量必须用同一套屏蔽值。
    // 之前只有位置修正屏蔽了睡眠体, 冲量却仍用原始 invMass/invI, 于是:
    // 睡眠块被施加冲量、速度凭空增长, 但位置又被按住不动 → 速度涨过唤醒阈值
    // 就醒过来乱撞 → 下一次碰撞再注入更多能量……积木越撞越快(实测 2 秒内
    // 从 v=300 涨到 v=1550), 全场永远静不下来, 玩家就得一直等。
    const bool aAwake = !(A.sleeping && *A.sleeping);
    const bool bAwake = !(B.sleeping && *B.sleeping);
    const qreal invA = aAwake ? A.invMass : 0.0;
    const qreal invB = bAwake ? B.invMass : 0.0;
    const qreal invIA = aAwake ? A.invI : 0.0;
    const qreal invIB = bAwake ? B.invI : 0.0;
    const qreal corrInv = invA + invB;
    if (corrInv > 1e-9) {
        const QPointF corr = nrm * (pen * 0.40 / corrInv);
        *A.pos -= corr * invA;
        *B.pos += corr * invB;
    }

    const QPointF rA = contact - *A.pos;
    const QPointF rB = contact - *B.pos;
    const QPointF vA = *A.vel + (A.angVel ? crossSV(*A.angVel, rA) : QPointF(0, 0));
    const QPointF vB = *B.vel + (B.angVel ? crossSV(*B.angVel, rB) : QPointF(0, 0));
    const QPointF rv = vB - vA;
    const qreal vn = rv.x() * nrm.x() + rv.y() * nrm.y();
    if (outApproach) *outApproach = vn;
    if (vn > 0) return 0.0;                     // 正在分离

    const qreal ra = A.angVel ? cross2(rA, nrm) : 0.0;
    const qreal rb = B.angVel ? cross2(rB, nrm) : 0.0;
    // 分母和施加都必须用屏蔽后的 invA/invIA(睡眠体=静态), 否则又会把速度灌给睡眠块
    const qreal den = invA + invB + invIA * ra * ra + invIB * rb * rb;
    if (den < 1e-9) return 0.0;

    const qreal jn = -(1.0 + e) * vn / den;
    const QPointF J = nrm * jn;
    *A.vel -= J * invA;
    if (A.angVel && invIA > 0.0) *A.angVel -= invIA * cross2(rA, J);
    *B.vel += J * invB;
    if (B.angVel && invIB > 0.0) *B.angVel += invIB * cross2(rB, J);

    // --- 切向摩擦 ---
    const QPointF t(-nrm.y(), nrm.x());
    const qreal vt = rv.x() * t.x() + rv.y() * t.y();
    const qreal ta = A.angVel ? cross2(rA, t) : 0.0;
    const qreal tb = B.angVel ? cross2(rB, t) : 0.0;
    const qreal den2 = invA + invB + invIA * ta * ta + invIB * tb * tb;
    if (den2 > 1e-9) {
        qreal jt = -vt / den2;
        const qreal maxF = mu * qAbs(jn);
        jt = clampv(jt, -maxF, maxF);
        const QPointF Jt = t * jt;
        *A.vel -= Jt * invA;
        if (A.angVel && invIA > 0.0) *A.angVel -= invIA * cross2(rA, Jt);
        *B.vel += Jt * invB;
        if (B.angVel && invIB > 0.0) *B.angVel += invIB * cross2(rB, Jt);
    }

    // 唤醒判据用"接近速度", 绝不能用冲量:
    // 一头猪安静地趴在木梁上, 每帧重力也产生 jn≈40 的支撑冲量, 用冲量判断
    // 就会每帧把木梁唤醒, 于是没人碰它整座塔自己就塌了。
    // 真正的撞击接近速度是几百, 静置只有十几, 阈值取 70 分得很开。
    if (-vn > 70.0) {
        if (A.sleeping) *A.sleeping = false;
        if (B.sleeping) *B.sleeping = false;
    }
    return qAbs(jn);
}

// 点在某旋转矩形内（返回穿透深度，负值表示在外）
qreal pointInBoxDepth(const QPointF &p, const QPointF &c, const QSizeF &s, qreal a)
{
    const QPointF d = rotv(p - c, -a);
    const qreal hx = s.width() * 0.5, hy = s.height() * 0.5;
    const qreal dx = hx - qAbs(d.x());
    const qreal dy = hy - qAbs(d.y());
    if (dx < 0 || dy < 0) return -1.0;
    return qMin(dx, dy);
}

// 圆 vs 旋转矩形：nrm 由矩形指向圆
bool circleVsBox(const QPointF &cc, qreal cr,
                 const QPointF &bc, const QSizeF &bs, qreal ba,
                 QPointF &nrm, qreal &pen, QPointF &contact)
{
    const QPointF local = rotv(cc - bc, -ba);
    const qreal hx = bs.width() * 0.5, hy = bs.height() * 0.5;
    const QPointF closest(clampv(local.x(), -hx, hx), clampv(local.y(), -hy, hy));
    const QPointF d = local - closest;
    const qreal dist2 = d.x() * d.x() + d.y() * d.y();

    if (dist2 > cr * cr) return false;

    if (dist2 < 1e-6) {
        // 圆心落在矩形内部 —— 沿最小穿透方向推出
        const qreal dx = hx - qAbs(local.x());
        const qreal dy = hy - qAbs(local.y());
        if (dx < dy) {
            nrm = rotv(QPointF(local.x() >= 0 ? 1.0 : -1.0, 0), ba);
            pen = dx + cr;
        } else {
            nrm = rotv(QPointF(0, local.y() >= 0 ? 1.0 : -1.0), ba);
            pen = dy + cr;
        }
        contact = bc + rotv(closest, ba);
    } else {
        const qreal dist = std::sqrt(dist2);
        nrm = rotv(d / dist, ba);
        pen = cr - dist;
        contact = bc + rotv(closest, ba);
    }
    return pen > 0;
}

// 旋转矩形 vs 旋转矩形（简化 SAT：取 A 的四条法线）
void boxCornersOf(const QPointF &c, const QSizeF &s, qreal a, QPointF out[4])
{
    const qreal hx = s.width() * 0.5, hy = s.height() * 0.5;
    out[0] = c + rotv(QPointF(-hx, -hy), a);
    out[1] = c + rotv(QPointF( hx, -hy), a);
    out[2] = c + rotv(QPointF( hx,  hy), a);
    out[3] = c + rotv(QPointF(-hx,  hy), a);
}

bool boxVsBox(const QPointF &ac, const QSizeF &as, qreal aa,
              const QPointF &bc, const QSizeF &bs, qreal ba,
              QPointF &nrm, qreal &pen, QPointF &contact)
{
    QPointF ca[4], cb[4];
    boxCornersOf(ac, as, aa, ca);
    boxCornersOf(bc, bs, ba, cb);

    const QPointF axes[4] = {
        rotv(QPointF(1, 0), aa), rotv(QPointF(0, 1), aa),
        rotv(QPointF(-1, 0), aa), rotv(QPointF(0, -1), aa)
    };

    qreal best = 1e30;
    int  bestAxis = -1;
    for (int k = 0; k < 4; ++k) {
        const QPointF &ax = axes[k];
        qreal minA = 1e30, maxA = -1e30, minB = 1e30, maxB = -1e30;
        for (int i = 0; i < 4; ++i) {
            qreal d = ca[i].x() * ax.x() + ca[i].y() * ax.y();
            minA = qMin(minA, d); maxA = qMax(maxA, d);
            d = cb[i].x() * ax.x() + cb[i].y() * ax.y();
            minB = qMin(minB, d); maxB = qMax(maxB, d);
        }
        const qreal overlap = qMin(maxA, maxB) - qMax(minA, minB);
        if (overlap <= 0) return false;
        if (overlap < best) { best = overlap; bestAxis = k; }
    }
    if (bestAxis < 0) return false;

    nrm = axes[bestAxis];
    if ((bc - ac).x() * nrm.x() + (bc - ac).y() * nrm.y() < 0) nrm = -nrm;
    pen = best;

    // 接触点：两边各自"陷得最深"的顶点取平均
    qreal dA = -1, dB = -1;
    QPointF pA, pB;
    for (int i = 0; i < 4; ++i) {
        qreal d = pointInBoxDepth(cb[i], ac, as, aa);
        if (d > dA) { dA = d; pA = cb[i]; }
    }
    for (int i = 0; i < 4; ++i) {
        qreal d = pointInBoxDepth(ca[i], bc, bs, ba);
        if (d > dB) { dB = d; pB = ca[i]; }
    }
    if (dA >= 0 && dB >= 0)      contact = (pA + pB) * 0.5;
    else if (dA >= 0)            contact = pA;
    else if (dB >= 0)            contact = pB;
    else                         contact = (ac + bc) * 0.5;
    return true;
}

inline bool circlesOverlap(const QPointF &a, qreal ar, const QPointF &b, qreal br,
                           QPointF &nrm, qreal &pen, QPointF &contact)
{
    const QPointF d = b - a;
    const qreal dist = std::sqrt(d.x() * d.x() + d.y() * d.y());
    const qreal sum = ar + br;
    if (dist >= sum) return false;
    pen = sum - dist;
    nrm = (dist > 1e-6) ? (d / dist) : QPointF(0, -1);
    contact = a + nrm * (ar - pen * 0.5);
    return true;
}

// 材质配色
struct MatStyle { QColor base, dark, light; };
MatStyle matStyle(int mat)
{
    MatStyle s;
    if (mat == 0) {                       // 木头
        s.base  = QColor(0xC8, 0x8B, 0x45);
        s.dark  = QColor(0x8B, 0x5A, 0x22);
        s.light = QColor(0xE8, 0xB6, 0x78);
    } else if (mat == 1) {                // 石头
        s.base  = QColor(0xA9, 0xB2, 0xBC);
        s.dark  = QColor(0x6B, 0x76, 0x83);
        s.light = QColor(0xD3, 0xDA, 0xE2);
    } else if (mat == 2) {                // 玻璃
        s.base  = QColor(0x9E, 0xE0, 0xF0, 175);
        s.dark  = QColor(0x4F, 0xA8, 0xC4, 200);
        s.light = QColor(0xE8, 0xFB, 0xFF, 230);
    } else {                              // 深色木箱 / TNT
        s.base  = QColor(0x9C, 0x5B, 0x2A);
        s.dark  = QColor(0x63, 0x35, 0x14);
        s.light = QColor(0xC5, 0x83, 0x4B);
    }
    return s;
}

} // anonymous namespace

// ============================================================================
//  第三部分：构造 / 析构
// ============================================================================
game::game(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::game)
{
    ui->setupUi(this);

    // 整屏都由 paintEvent 亲手画满, 告诉 Qt 不用再擦背景 —— 少一次全屏填充
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, true);

    // 关键: 关掉游戏绝不能连带把整个程序退掉。
    // Qt 默认会给顶层窗口置上 WA_QuitOnClose, 而 QWidgetPrivate::close_helper() 里
    // 的判定是(Qt5.12 源码 qwidget.cpp):
    //     quitOnClose = quitOnClose && (parent.isNull() || !parent->isVisible());
    // 只要父窗口那一刻被判定为"不可见", game 被 close() 时 Qt 就当成
    // "最后一个窗口已关闭", 走 emitLastWindowClosed() + maybeQuit() 直接退出整个 app。
    // 板子跑 linuxfb, 没有窗口管理器帮我们维护窗口可见性, 这套判定尤其容易踩中
    // —— 表现就是"关游戏"变成了"关程序"。这里显式豁免, 让 close() 只关这一层窗口。
    setAttribute(Qt::WA_QuitOnClose, false);

    // 板上是 linuxfb 上没有窗口管理器, 鼠标指针会一直杵在画面上, 直接藏掉
    setCursor(Qt::BlankCursor);

    // 顶部按钮热区。必须显式给矩形: QRectF 默认是 0x0,
    // 不给的话按钮既画不出来也点不到, 游戏就退不出去了。
    layoutHudButtons();

    initAudio();

    m_best = 1;
    startLevel(0);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &game::onTick);
    m_timer->start(kFrameMs);           // 40fps
}

game::~game()
{
    // ---- 关游戏会崩溃的根因就在这里, 顺序不能乱 ----
    // m_audio / m_audioOut / m_timer 都是 game 的 QObject 子对象, 归 deleteChildren()
    // 按声明顺序析构。之前只 stop() 了音频, 于是 GameAudio 先被释放; 等轮到
    // QAudioSink 析构时, QtMultimedia 内部会再转一圈事件循环, 把事件队列里还堆着的
    // timeout 事件派发出去 —— 已经半析构的 this 收到 onTick(), 执行到
    //     m_audio->setMusicOn(...)
    // 就是往已释放的 GameAudio 上写数据(ASan: heap-use-after-free, WRITE of size 1,
    // game.cpp:936 -> game.cpp:99)。堆被写坏后, 下一次 malloc 检测到损坏就 abort(),
    // 整个程序瞬间消失、屏幕上什么都不剩 —— 表现就是"关游戏把整个 app 关了"。
    //
    // 修法: 先把定时器停掉断开(不再有新事件), 再主动按 AudioSink -> GameAudio 的
    // 顺序释放, 别让 deleteChildren() 的默认顺序替我们决定。
    if (m_timer) {
        m_timer->stop();
        m_timer->disconnect();
    }
    if (m_audioOut) {
        m_audioOut->stop();
        disconnect(m_audioOut, nullptr, this, nullptr);
        delete m_audioOut;          // 子对象, delete 会自动从父对象摘除
        m_audioOut = nullptr;
    }
    if (m_audio) {
        disconnect(m_audio, nullptr, this, nullptr);
        m_audio->close();
        delete m_audio;
        m_audio = nullptr;
    }
    m_audioDev = nullptr;           // 由 QAudioSink 持有, 不归我们释放

    delete ui;
}

// ---- 音频初始化 -------------------------------------------------------------
void game::initAudio()
{
    QAudioFormat fmt;
    fmt.setSampleRate(kRate);
    fmt.setChannelCount(1);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Qt6 的 QAudioFormat 去掉了 codec/字节序/采样位数的 setter，
    // 只保留 setSampleFormat(SampleFormat::Int16) 这一个入口
    fmt.setSampleFormat(QAudioFormat::Int16);
#else
    fmt.setSampleSize(16);
    fmt.setCodec("audio/pcm");
    fmt.setByteOrder(QAudioFormat::LittleEndian);
    fmt.setSampleType(QAudioFormat::SignedInt);
#endif

    m_audio = new GameAudio(this);
    m_audio->open(QIODevice::ReadOnly);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
#else
    const QAudioDeviceInfo dev = QAudioDeviceInfo::defaultOutputDevice();
#endif
    // 板子的 linuxfb 环境不一定有声卡，没有就静音跑，绝不让游戏挂掉
    if (dev.isNull() || !dev.isFormatSupported(fmt)) {
        qDebug() << "[game] 没有可用的音频输出设备，静音运行";
        return;
    }

    m_audioOut = new GameAudioSink(dev, fmt, this);
    m_audioOut->setBufferSize(kRate * 2 / 4);
    m_audioDev = m_audioOut->start();
    if (!m_audioDev)
        qDebug() << "[game] 音频设备打开失败，静音运行";
}

void game::playSfx(int type)
{
    if (m_audio) m_audio->trigger(type);
}

// ============================================================================
//  第四部分：关卡搭建
// ============================================================================
namespace {

// 积木质量按材质的密度算，避免手写一堆常数
void setupBlock(Block &b, qreal w, qreal h, int mat, int kind)
{
    b.size  = QSizeF(w, h);
    b.mat   = mat;
    b.kind  = kind;
    const qreal area = w * h;
    if (mat == 0)       b.mass = qMax(1.0, area * 0.0016);   // 木
    else if (mat == 1)  b.mass = qMax(2.5, area * 0.0034);   // 石
    else                b.mass = qMax(0.8, area * 0.0009);   // 玻璃
    if (kind == 2)      b.mass *= 0.9;                       // TNT 轻一点，炸得飞
    b.invMass = 1.0 / b.mass;
    const qreal I = b.mass * (w * w + h * h) / 12.0;
    b.invI = (I > 1e-6) ? 1.0 / I : 0.0;
    b.hp = (mat == 0) ? 68 : (mat == 1 ? 190 : 26);
    if (kind == 1) b.hp = 110;                                // 木箱更结实
    if (kind == 2) b.hp = 20;                                 // TNT 一碰就炸
    b.sleeping = true;
    b.seed = b.pos.x() * 0.37 + b.pos.y() * 1.13;
}

} // namespace

void game::startLevel(int idx)
{
    m_level = idx;

    m_queue.clear();
    m_queueAll.clear();
    m_birds.clear();
    m_pigs.clear();
    m_blocks.clear();
    m_particles.clear();
    m_debris.clear();

    m_settleT = 0;
    m_winT    = 0;
    m_pulling = false;
    m_hudHintT = 4.5;
    m_graceT  = 1.5;          // 开场保护期, 见 game.h 里的说明
    m_state   = StAim;

    if (idx == 0)      buildLevel1();
    else if (idx == 1) buildLevel2();
    else               buildLevel3();

    m_pigsTotal = m_pigs.size();

    // 静态背景和分辨率有关, 第一次进来(或尺寸变过)时重建;
    // 场景层(背景+猪+积木+弹弓)则在每次换关时整体重烘
    if (m_bgCache.isNull() || !m_bgCache.size().isValid()) buildStaticCache();
    rebuildSceneLayer();
    update();
}

void game::buildLevel1()
{
    // 三只小鸟：小红 / 小黄 / 小蓝
    m_queueAll << QColor(0xE0, 0x3A, 0x2B)
               << QColor(0xF5, 0xC5, 0x11)
               << QColor(0x2E, 0x9B, 0xE0);
    m_queue = m_queueAll;

    // 地上蹲一只
    { Pig g; g.pos = QPointF(752, World::GROUND_Y - World::PIG_R); g.idx = 0; m_pigs.push_back(g); }

    // 小木屋：两根立柱 + 一根横梁
    Block b;
    b.pos = QPointF(700, 460); setupBlock(b, 20, 120, 0, 0); m_blocks.push_back(b);
    b.pos = QPointF(804, 460); setupBlock(b, 20, 120, 0, 0); m_blocks.push_back(b);
    b.pos = QPointF(752, 392); setupBlock(b, 136, 18, 0, 0); m_blocks.push_back(b);

    // 屋顶上的猪
    { Pig g; g.pos = QPointF(752, 363); g.idx = 1; m_pigs.push_back(g); }
}

void game::buildLevel2()
{
    m_queueAll << QColor(0xE0, 0x3A, 0x2B)
               << QColor(0x2E, 0x9B, 0xE0)
               << QColor(0xF5, 0xC5, 0x11)
               << QColor(0xE0, 0x3A, 0x2B);
    m_queue = m_queueAll;

    const qreal gy = World::GROUND_Y;
    Block b;

    // 摆放规则（很重要，写死在这里免得以后又算错）：
    //   叠放时，上面那件的"底面"要正好等于下面那件的"顶面"，一个像素都不能差。
    //   差 1~2px 就会：互相嵌入 → 求解器每帧把对方顶开 → 结构抖动 →
    //   产生接近速度 → 持续扣血。TNT 只有 20 点血，一两秒就被磨爆，
    //   表现成"还没玩，炸弹自己就炸了"。
    //   底面 = pos.y + h/2      顶面 = pos.y - h/2
    //   → 想让"高 h 的物件底面落在 Y 上"，应取 pos.y = Y - h/2

    // ---- 左侧玻璃塔 ----
    // 玻璃柱 h=120, 底面贴地 gy → y = gy-60, 柱顶 = gy-120
    b.pos = QPointF(650, gy - 60);  setupBlock(b, 14, 120, 2, 0); m_blocks.push_back(b);
    b.pos = QPointF(706, gy - 60);  setupBlock(b, 14, 120, 2, 0); m_blocks.push_back(b);
    // 木梁 h=16, 底面贴柱顶 gy-120 → y = gy-128, 梁顶 = gy-136
    b.pos = QPointF(678, gy - 128); setupBlock(b, 76, 16, 0, 0);  m_blocks.push_back(b);
    // 猪 r=20, 底面贴梁顶 gy-136 → y = gy-156
    { Pig g; g.pos = QPointF(678, gy - 156); g.idx = 0; m_pigs.push_back(g); }

    // ---- 中央石堡 ----
    // 石柱 h=110 → y = gy-55, 柱顶 = gy-110
    b.pos = QPointF(820, gy - 55);  setupBlock(b, 26, 110, 1, 0); m_blocks.push_back(b);
    b.pos = QPointF(900, gy - 55);  setupBlock(b, 26, 110, 1, 0); m_blocks.push_back(b);
    // 石梁 h=24, 底面贴柱顶 gy-110 → y = gy-122, 梁顶 = gy-134
    b.pos = QPointF(860, gy - 122); setupBlock(b, 106, 24, 1, 0); m_blocks.push_back(b);
    // TNT 箱 h=44, 底面贴地 → y = gy-22  (原来写成 gy-24, 悬空 2px)
    b.pos = QPointF(860, gy - 22);  setupBlock(b, 44, 44, 0, 2);  m_blocks.push_back(b);
    // 木箱 h=40, 底面贴梁顶 gy-134 → y = gy-154  (原来 gy-152, 与石梁重叠 2px ← 自爆元凶)
    b.pos = QPointF(860, gy - 154); setupBlock(b, 46, 40, 0, 1);  m_blocks.push_back(b);
    // 猪 r=20, 底面贴木箱顶 gy-174 → y = gy-194  (原来 gy-196, 悬空)
    { Pig g; g.pos = QPointF(860, gy - 194); g.idx = 1; m_pigs.push_back(g); }
    { Pig g; g.pos = QPointF(944, gy - World::PIG_R); g.idx = 2; m_pigs.push_back(g); }
}

void game::buildLevel3()
{
    m_queueAll << QColor(0xE0, 0x3A, 0x2B)
               << QColor(0x2E, 0x9B, 0xE0)
               << QColor(0x2E, 0x9B, 0xE0)
               << QColor(0xF5, 0xC5, 0x11);
    m_queue = m_queueAll;

    const qreal gy = World::GROUND_Y;
    Block b;

    // ---- 前哨：两格小木塔 ----
    // 木柱 h=84 → y = gy-42, 柱顶 = gy-84
    b.pos = QPointF(600, gy - 42);  setupBlock(b, 16, 84, 0, 0); m_blocks.push_back(b);
    b.pos = QPointF(668, gy - 42);  setupBlock(b, 16, 84, 0, 0); m_blocks.push_back(b);
    // 木梁 h=18, 底面贴柱顶 gy-84 → y = gy-93, 梁顶 = gy-102
    b.pos = QPointF(634, gy - 93);  setupBlock(b, 92, 18, 0, 0); m_blocks.push_back(b);
    // 猪 r=20, 底面贴梁顶 gy-102 → y = gy-122  (原来 gy-118, 嵌入梁里 4px)
    { Pig g; g.pos = QPointF(634, gy - 122); g.idx = 0; m_pigs.push_back(g); }

    // ---- 主城：石砌双层 ----
    // 一层：石柱 h=110 → y = gy-55, 柱顶 = gy-110
    b.pos = QPointF(800, gy - 55);  setupBlock(b, 28, 110, 1, 0); m_blocks.push_back(b);
    b.pos = QPointF(960, gy - 55);  setupBlock(b, 28, 110, 1, 0); m_blocks.push_back(b);
    // 石梁 h=24, 底面贴柱顶 gy-110 → y = gy-122, 梁顶 = gy-134
    b.pos = QPointF(880, gy - 122); setupBlock(b, 182, 24, 1, 0); m_blocks.push_back(b);
    { Pig g; g.pos = QPointF(852, gy - World::PIG_R); g.idx = 1; m_pigs.push_back(g); }
    { Pig g; g.pos = QPointF(912, gy - World::PIG_R); g.idx = 2; m_pigs.push_back(g); }
    // 二层：玻璃墙 h=76, 底面贴梁顶 gy-134 → y = gy-172, 墙顶 = gy-210
    b.pos = QPointF(826, gy - 172); setupBlock(b, 14, 76, 2, 0);  m_blocks.push_back(b);
    b.pos = QPointF(934, gy - 172); setupBlock(b, 14, 76, 2, 0);  m_blocks.push_back(b);
    // 木箱 h=44, 底面贴梁顶 gy-134 → y = gy-156  (原来 gy-156, 本来就对)
    b.pos = QPointF(880, gy - 156); setupBlock(b, 46, 44, 0, 1);  m_blocks.push_back(b);
    // 顶上石梁 h=18, 底面贴墙顶 gy-210 → y = gy-219, 梁顶 = gy-228
    b.pos = QPointF(880, gy - 219); setupBlock(b, 132, 18, 1, 0); m_blocks.push_back(b);
    // 猪 r=20, 底面贴梁顶 gy-228 → y = gy-248  (原来 gy-252, 悬空)
    { Pig g; g.pos = QPointF(880, gy - 248); g.idx = 3; m_pigs.push_back(g); }
    // 城下最后一头
    { Pig g; g.pos = QPointF(1002, gy - World::PIG_R); g.idx = 4; m_pigs.push_back(g); }
}

// ============================================================================
//  第五部分：游戏逻辑
// ============================================================================
QPointF game::slingAnchor() const
{
    return QPointF(150.0, 393.0);
}

QPointF game::slingBirdPos() const
{
    if (m_pulling) return m_pullPos;
    return slingAnchor();
}

void game::launchBird(const QPointF &vel)
{
    if (m_queue.isEmpty()) return;

    Bird b;
    b.pos      = slingBirdPos();
    b.vel      = vel;
    b.r        = World::BIRD_R;
    b.color    = m_queue.first();
    b.flying   = true;
    b.alive    = true;
    b.rot      = std::atan2(vel.y(), vel.x());
    b.trail.clear();
    m_birds.push_back(b);
    m_queue.removeFirst();

    playSfx(0);
    emitParticles(b.pos, 10, 2, 1.2, 190, QColor(0xFF, 0xFF, 0xFF), 4.0);

    m_state   = StFly;
    m_pulling = false;
    m_settleT = 0;
    m_hudHintT = 0;
    m_lastShotT = m_time;
}

void game::addScore(int v)
{
    m_score += v;
    if (m_score > m_best) m_best = m_score;
}

int game::pigsAliveCount() const
{
    int n = 0;
    for (const Pig &p : m_pigs) if (p.alive) ++n;
    return n;
}

void game::emitParticles(const QPointF &p, int n, int type, qreal spread,
                         qreal speed, const QColor &c, qreal size)
{
    for (int i = 0; i < n; ++i) {
        Particle pt;
        const qreal a = (qreal)((i * 2654435761u) % 1000) / 1000.0 * 6.2831853;
        const qreal v = speed * (0.35 + 0.65 * ((i * 40503u % 997) / 997.0));
        pt.pos    = p;
        pt.vel    = QPointF(std::cos(a) * v, std::sin(a) * v - spread * 6.0);
        pt.maxLife = 0.45 + 0.65 * ((i * 7919u % 100) / 100.0);
        pt.life    = pt.maxLife;
        pt.size    = size * (0.6 + 0.7 * ((i * 104729u % 100) / 100.0));
        pt.color   = c;
        pt.type    = type;
        pt.rot     = (qreal)(i % 12) * 0.523;
        pt.spin    = ((i % 2) ? 1 : -1) * (2.0 + (i % 5));
        m_particles.push_back(pt);
    }
}

void game::explode(const QPointF &p, qreal radius, qreal power)
{
    playSfx(4);

    emitParticles(p, 34, 1, 2.2, 460, QColor(0xFF, 0xD3, 0x4F), 9.0);
    emitParticles(p, 22, 5, 1.6, 220, QColor(0x60, 0x60, 0x60, 170), 16.0);
    emitParticles(p, 18, 0, 2.0, 380, QColor(0x8B, 0x5A, 0x22), 5.0);

    // 冲击波：把周围的东西都掀飞
    for (Block &b : m_blocks) {
        if (!b.alive) continue;
        const QPointF d = b.pos - p;
        const qreal dist = std::sqrt(d.x() * d.x() + d.y() * d.y());
        if (dist > radius) continue;
        const qreal f = (1.0 - dist / radius);
        const QPointF dir = unit(d);
        b.sleeping = false;
        b.vel    += dir * (power * f * 900.0);
        b.vel.ry() -= power * f * 380.0;
        b.angVel += (dir.x() >= 0 ? 1 : -1) * f * 9.0;
        damageBlock(b, 220.0 * f + 40.0);
    }
    for (Pig &g : m_pigs) {
        if (!g.alive) continue;
        const QPointF d = g.pos - p;
        const qreal dist = std::sqrt(d.x() * d.x() + d.y() * d.y());
        if (dist > radius) continue;
        const qreal f = (1.0 - dist / radius);
        g.pos += unit(d) * (f * 12.0);
        g.vel += unit(d) * (power * f * 760.0);
        g.vel.ry() -= power * f * 330.0;
    }
    for (Bird &b : m_birds) {
        if (!b.alive) continue;
        const QPointF d = b.pos - p;
        const qreal dist = std::sqrt(d.x() * d.x() + d.y() * d.y());
        if (dist > radius) continue;
        const qreal f = (1.0 - dist / radius);
        b.vel += unit(d) * (power * f * 700.0);
    }
}

void game::killPig(int i, qreal power)
{
    Pig &g = m_pigs[i];
    if (!g.alive) return;
    g.alive = false;

    playSfx(2);
    const int pts = 5000;
    addScore(pts);

    // 猪被打飞出去（视觉上飞出去再消失）
    emitParticles(g.pos, 20, 1, 1.2, 260, QColor(0xFF, 0xFF, 0xFF), 6.0);
    emitParticles(g.pos, 16, 0, 1.6, 300, QColor(0x7E, 0xD3, 0x5A), 4.5);

    Q_UNUSED(power)
}

void game::damageBlock(Block &b, qreal power)
{
    if (!b.alive) return;
    if (m_graceT > 0) return;     // 开场保护期: 结构自己贴合的那点误差不结算伤害
    b.hp -= (int)power;
    b.hurtT = 0.18;
    if (b.hp > 0) return;

    b.alive = false;

    const MatStyle st = matStyle(b.mat);

    if (b.kind == 2) {                       // TNT
        addScore(500);
        explode(b.pos, 165.0, 1.25);
        return;
    }

    addScore(b.mat == 1 ? 300 : 200);
    if (b.mat == 2) {
        playSfx(3);
        emitParticles(b.pos, 18, 0, 1.4, 300, QColor(0xC8, 0xF2, 0xFF), 5.0);
    } else {
        playSfx(1);
        emitParticles(b.pos, 12, 0, 1.2, 240, st.base, 5.5);
    }

    // 碎块：把整块拆成 3~5 片飞出去
    const int n = 3 + (int)((b.size.width() + b.size.height()) / 60.0);
    for (int k = 0; k < qMin(n, 6); ++k) {
        Plank pl;
        pl.size  = QSizeF(b.size.width() * (0.30 + 0.14 * k * 0.3),
                          qMax(5.0, b.size.height() * 0.26));
        pl.pos   = b.pos + rotv(QPointF((k - n * 0.5) * 9.0, (k % 2 ? -6 : 6)), b.angle);
        pl.angle = b.angle + (k - n * 0.5) * 0.35;
        pl.angVel = (k % 2 ? 1 : -1) * (2.4 + k * 0.55);
        pl.life  = 2.2;
        pl.mat   = b.mat;
        m_debris.push_back(pl);
    }

    // 唤醒附近的积木，避免“下面塌了上面还悬着”
    for (Block &o : m_blocks) {
        if (!o.alive || o.sleeping == false) continue;
        if (QPointF(o.pos - b.pos).manhattanLength() < 190.0) o.sleeping = false;
    }
}

void game::onTick()
{
    const qreal dt = kFrameDt;
    m_time += dt;

    const qreal hintBefore = m_hudHintT;
    if (m_hudHintT > 0) m_hudHintT -= dt;
    if (m_graceT  > 0) m_graceT  -= dt;      // 开场保护期倒计时
    if (m_audio)        m_audio->setMusicOn(m_musicOn);

    // 受击闪白衰减
    for (Block &b : m_blocks) if (b.hurtT > 0) b.hurtT -= dt;
    for (Pig &g : m_pigs)     if (g.hurtT > 0) g.hurtT -= dt;

    // 场景层只烘"睡着"的积木。醒着的积木(塌方时最多几块)每帧实时画。
    // 所以只有"有积木刚好睡着/醒过来"时才需要重烘 —— 每次塌方只发生几次,
    // 而不是每帧一次, 这才是省时间的关键。
    bool bakeChanged = false;

    // (a) 积木"在动/不动"状态变了
    if (m_bakedStill.size() != m_blocks.size()) {
        bakeChanged = true;
    } else {
        for (int i = 0; i < m_blocks.size(); ++i) {
            const bool now = (m_blocks[i].alive && blockIsStill(m_blocks[i]));
            if (m_bakedStill[i] != now) { bakeChanged = true; break; }
        }
    }

    // (b) 弹弓上那只待发的鸟变了。
    //     场景层里烘了"弹弓上的鸟", 如果只在鸟的颜色/数量变化时才重烘是不够的 ——
    //     打完一只红鸟、下一只还是红鸟时颜色没变, 缓存里那张红鸟就留在弹弓上,
    //     看起来就像发射后留了个分身。所以把"鸟总数+当前鸟色+是否瞄准中"整体作为判据。
    if (!bakeChanged) {
        const int total = m_queueAll.size();
        const int left  = m_queue.size();
        const uint birdKey = (uint)((m_state == StAim && !m_queue.isEmpty())
                                    ? (m_queue.first().rgb() ^ (left * 7919) ^ (total * 104729))
                                    : 0xDEADBEEFu);
        if (birdKey != m_bakedBirdKey) {
            m_bakedBirdKey = birdKey;
            bakeChanged = true;
        }
    }

    if (bakeChanged) rebuildSceneLayer();

    // 物理跑子步: 精度按 80Hz 算, 渲染仍是 40fps
    for (int s = 0; s < kSubSteps; ++s) stepPhysics(dt / kSubSteps);

    // 粒子
    for (int i = m_particles.size() - 1; i >= 0; --i) {
        Particle &p = m_particles[i];
        p.life -= dt;
        if (p.life <= 0) { m_particles.removeAt(i); continue; }
        if (p.type == 5) {                    // 烟：慢慢上飘
            p.vel *= (1.0 - 1.2 * dt);
            p.vel.ry() -= 22.0 * dt;
            p.size += 16.0 * dt;
        } else if (p.type == 2 || p.type == 3) {   // 彩纸/羽毛：飘
            p.vel.ry() += World::GRAVITY * 0.28 * dt;
            p.vel *= (1.0 - 1.6 * dt);
        } else {
            p.vel.ry() += World::GRAVITY * dt * 0.75;
        }
        p.pos += p.vel * dt;
        p.rot += p.spin * dt;
        if (p.type != 5 && p.pos.y() > World::GROUND_Y - 2 && p.type != 1) {
            p.pos.setY(World::GROUND_Y - 2);
            p.vel.setY(-p.vel.y() * 0.28);
            p.vel.setX(p.vel.x() * 0.7);
        }
    }

    // 碎块
    for (int i = m_debris.size() - 1; i >= 0; --i) {
        Plank &pl = m_debris[i];
        pl.life -= dt;
        if (pl.life <= 0) { m_debris.removeAt(i); continue; }
        pl.vel.ry() += World::GRAVITY * dt;
        pl.pos += pl.vel * dt;
        pl.angle += pl.angVel * dt;
        if (pl.pos.y() > World::GROUND_Y - 3) {
            pl.pos.setY(World::GROUND_Y - 3);
            pl.vel.setY(-pl.vel.y() * 0.25);
            pl.vel.setX(pl.vel.x() * 0.78);
            pl.angVel *= 0.7;
        }
    }

    const int before = m_state;
    handleRoundEnd(dt);

    // 只有在"画面上确实有东西在动"的时候才重绘。
    // 结算画面完全静止的话, 每秒 40 次全屏重画纯属浪费 CPU。
    bool moving = m_pulling
               || m_state != before                 // 状态机切换(瞄准/飞行/结算)
               || m_state == StFly
               || !m_particles.isEmpty()
               || !m_debris.isEmpty()
               || m_winT < 0.5                      // 结算卡片入场动画
               || m_hudHintT > 0 || hintBefore > 0; // 开局提示还在显示

    // 关键: 积木也必须算进来。漏掉它的话, 一堆积木在原地抖的时候
    // 我们判定"没东西在动" → 不重绘 → 画面就卡死在那一帧。
    if (!moving)
        for (const Block &b : m_blocks)
            if (b.alive && !b.sleeping
                && (b.vel.manhattanLength() > 2.0 || qAbs(b.angVel) > 0.05)) { moving = true; break; }
    if (!moving)
        for (const Bird &b : m_birds)
            if (b.alive && b.vel.manhattanLength() > 2.0) { moving = true; break; }
    if (!moving)
        for (const Pig &g : m_pigs)
            if (g.alive && g.vel.manhattanLength() > 2.0) { moving = true; break; }

    if (moving) update();
}

// ---- 物理步进 ---------------------------------------------------------------
void game::stepPhysics(qreal dt)
{
    // ---------- 1. 积分 ----------
    for (Bird &b : m_birds) {
        if (!b.alive) continue;
        if (b.flapT < 1e9) b.flapT += dt;
        if (!b.flying) continue;
        b.lifeT += dt;
        b.vel.ry() += World::GRAVITY * dt;
        b.vel *= (1.0 - World::BIRD_DRAG * dt);
        b.pos += b.vel * dt;
        b.rot  = std::atan2(b.vel.y(), b.vel.x()) * 0.85;
        if (b.vel.x() > -30.0) {                       // 只记录向右飞的轨迹
            b.trail.push_back(b.pos);
            if (b.trail.size() > 70) b.trail.removeFirst();
        }
    }

    for (Block &b : m_blocks) {
        if (!b.alive || b.sleeping) continue;
        b.vel.ry() += World::GRAVITY * dt;
        b.vel *= (1.0 - 0.28 * dt);
        b.angVel *= (1.0 - 0.55 * dt);
        b.pos += b.vel * dt;
        b.angle += b.angVel * dt;
    }

    for (Pig &g : m_pigs) {
        if (!g.alive) continue;
        g.vel *= (1.0 - 0.5 * dt);
        g.vel.ry() += World::GRAVITY * dt * 0.55;      // 猪没飞得那么夸张
        g.pos += g.vel * dt;
        g.blinkPhase += dt;
        if (g.squash < 1.0) g.squash = qMin(1.0, g.squash + dt * 2.2);

        // ---- 垂直支撑: 地面 和 它正下方的积木面, 取最高的那个 ----
        // 原来这里只看地面, 于是"站在塔上的猪"会一直往下穿, 直到卡进箱子/石梁里。
        // 关卡里大部分猪都是坐在积木上的, 所以必须让积木能托住它。
        qreal support = World::GROUND_Y;
        for (const Block &bk : m_blocks) {
            if (!bk.alive) continue;
            // 用猪心 x 反推该积木局部坐标; 不旋转或转得不多时才有意义
            const QPointF rel = g.pos - bk.pos;
            const QPointF loc = rotv(rel, -bk.angle);
            if (qAbs(loc.x()) > bk.size.width() * 0.5 + g.r * 0.6) continue;
            const qreal top    = bk.pos.y() - bk.size.height() * 0.5;   // 积木顶面
            const qreal bottom = bk.pos.y() + bk.size.height() * 0.5;
            // 只认"猪脚在积木顶面附近"的情况, 避免被旁边的积木误判
            if (g.pos.y() + g.r <= bottom + 2.0 && g.pos.y() + g.r >= top - 6.0)
                support = qMin(support, top);
        }
        if (g.pos.y() + g.r > support) {
            g.pos.setY(support - g.r);
            if (g.vel.y() > 120.0) g.squash = 0.78;
            g.vel.setY(0);
        }
        if (g.pos.x() < g.r) g.pos.setX(g.r);
        if (g.pos.x() > World::W - g.r) g.pos.setX(World::W - g.r);
    }

    // ---------- 2. 边界 ----------
    for (Bird &b : m_birds) {
        if (!b.alive || !b.flying) continue;
        const bool groundHit = (b.pos.y() + b.r >= World::GROUND_Y);
        if (groundHit) {
            b.pos.setY(World::GROUND_Y - b.r);
            if (b.vel.y() > 0) {
                if (b.vel.y() > 300.0) {
                    playSfx(1);
                    emitParticles(QPointF(b.pos.x(), World::GROUND_Y), 9, 0,
                                  1.4, 170, QColor(0x8B, 0x5E, 0x33), 4.5);
                }
                b.vel.setY(-b.vel.y() * 0.30);
            }
            b.vel.setX(b.vel.x() * 0.86);
            if (qAbs(b.vel.y()) < 45.0) b.vel.setY(0);
            if (qAbs(b.vel.x()) > 10.0) b.rot += b.vel.x() * dt / b.r;
        }
        if (b.pos.x() - b.r < 0)  { b.pos.setX(b.r); b.vel.setX(-b.vel.x() * 0.4); }
        // 右边不设墙: 力度够大就该让鸟飞出去(打飞的手感), 飞出去即离场
        if (b.pos.x() - b.r > World::W + 40.0) { b.alive = false; continue; }
        if (b.pos.y() - b.r < 0)  { b.pos.setY(b.r); b.vel.setY(-b.vel.y() * 0.4); }
    }

    // ---------- 3. 碰撞 ----------
    resolveCollisions();

    // ---------- 4. 休眠判定 ----------
    // 阈值太紧(之前 7 / 0.12)会让积木永远进不了睡眠: 一摞积木互相挤压时
    // 速度总在十几上下抖, 于是整摞永远在原地高频抖动, 看起来就是"卡住+振动"。
    // 阈值放宽 + 慢速时额外阻尼, 让它尽快静止下来。
    for (Block &b : m_blocks) {
        if (!b.alive || b.sleeping) continue;
        b.pos.setY(qMin(b.pos.y(), World::H + 300.0));

        const qreal sp = b.vel.manhattanLength();
        if (sp < 60.0) {                       // 慢速时加大阻尼, 把残余抖动吃掉
            b.vel *= (1.0 - qMin(0.6, 3.0 * dt));
            b.angVel *= (1.0 - qMin(0.6, 4.0 * dt));
        }

        if (sp < 26.0 && qAbs(b.angVel) < 0.7) {
            // 连续若干帧都够慢才允许睡, 免得在弹跳中间被误判
            if (++b.settleFrames > 3) {
                b.vel = QPointF(0, 0);
                b.angVel = 0;
                b.sleeping = true;
                b.settleFrames = 0;
            }
        } else {
            b.settleFrames = 0;
        }
    }

    // 掉出屏幕的鸟/猪直接清掉
    for (Bird &b : m_birds) if (b.alive && b.pos.y() > World::H + 200.0) b.alive = false;
}

void game::resolveCollisions()
{
    const qreal eBirdBox  = 0.40;
    const qreal eBirdPig  = 0.35;
    const qreal eBoxBox   = 0.16;
    const qreal mu        = 0.44;

    // -------- 积木 vs 地面 --------
    for (Block &b : m_blocks) {
        if (!b.alive) continue;
        QPointF c[4];
        boxCornersOf(b.pos, b.size, b.angle, c);
        qreal deepest = 0.0;
        QPointF deepP;
        int below = 0;
        for (int i = 0; i < 4; ++i) {
            if (c[i].y() > World::GROUND_Y) {
                ++below;
                if (c[i].y() - World::GROUND_Y > deepest) {
                    deepest = c[i].y() - World::GROUND_Y;
                    deepP = c[i];
                }
            }
        }
        if (!below) continue;

        // 睡眠中的积木: 只是摆得低了零点几像素的话, 悄悄抬回地面线就行,
        // 不能唤醒它 —— 唤醒就会落体、抖动, 把整座塔带塌。
        if (b.sleeping) {
            if (deepest < 2.5) {
                b.pos.setY(b.pos.y() - deepest);
                continue;
            }
            b.sleeping = false;
        }

        b.pos.setY(b.pos.y() - deepest);

        const QPointF rv = deepP - b.pos;
        const qreal rn = rv.x();
        const qreal den = b.invMass + b.invI * rn * rn;
        if (den > 1e-9) {
            const QPointF vp = b.vel + crossSV(b.angVel, rv);
            if (vp.y() > 0) {
                if (vp.y() > 320.0 && b.vel.manhattanLength() > 60.0) {
                    playSfx(1);
                }
                qreal j = -(1.0 + 0.12) * vp.y() / den;
                if (j < 0) j = 0;
                b.vel += QPointF(0, j * b.invMass);
                b.angVel += b.invI * rn * j;

                // 切向摩擦
                const QPointF vp2 = b.vel + crossSV(b.angVel, rv);
                const qreal vt = vp2.x();
                const qreal den2 = b.invMass + b.invI * (-rv.y()) * (-rv.y());
                if (den2 > 1e-9) {
                    qreal jt = -vt / den2;
                    const qreal mx = mu * qAbs(j);
                    jt = clampv(jt, -mx, mx);
                    b.vel += QPointF(jt * b.invMass, 0);
                    b.angVel += b.invI * (-rv.y()) * jt;
                }
            }
        }
    }

    // -------- 积木 vs 积木 --------
    const int nb = m_blocks.size();
    for (int a = 0; a < nb; ++a) {
        if (!m_blocks[a].alive) continue;
        for (int c = a + 1; c < nb; ++c) {
            Block &A = m_blocks[a];
            Block &B = m_blocks[c];
            if (!B.alive) continue;
            if (A.sleeping && B.sleeping) continue;
            if (A.sleeping && B.invMass <= 0.0) continue;
            if (B.sleeping && A.invMass <= 0.0) continue;

            QPointF nrm, contact;
            qreal pen = 0;
            if (!boxVsBox(A.pos, A.size, A.angle, B.pos, B.size, B.angle,
                          nrm, pen, contact)) continue;

            BodyRef ra, rb;
            ra.pos = &A.pos; ra.vel = &A.vel; ra.angVel = &A.angVel;
            ra.invMass = A.invMass; ra.invI = A.invI; ra.sleeping = &A.sleeping;
            rb.pos = &B.pos; rb.vel = &B.vel; rb.angVel = &B.angVel;
            rb.invMass = B.invMass; rb.invI = B.invI; rb.sleeping = &B.sleeping;

            qreal approach = 0;
            solveContact(ra, rb, pen, nrm, contact, eBoxBox, mu, &approach);
            // 只有"撞上来"才造成破坏；静止互相支撑不算
            const qreal hit = -approach - 105.0;
            if (hit > 0) {
                const qreal dmg = hit * 0.085;
                damageBlock(A, dmg);
                damageBlock(B, dmg);
            }
        }
    }

    // -------- 鸟 vs 积木 --------
    for (Bird &bd : m_birds) {
        if (!bd.alive) continue;
        for (Block &bk : m_blocks) {
            if (!bk.alive) continue;
            QPointF nrm, contact;
            qreal pen = 0;
            if (!circleVsBox(bd.pos, bd.r, bk.pos, bk.size, bk.angle, nrm, pen, contact))
                continue;

            BodyRef ra, rb;
            ra.pos = &bk.pos; ra.vel = &bk.vel; ra.angVel = &bk.angVel;
            ra.invMass = bk.invMass; ra.invI = bk.invI; ra.sleeping = &bk.sleeping;
            rb.pos = &bd.pos; rb.vel = &bd.vel; rb.angVel = nullptr;
            rb.invMass = 0.55; rb.invI = 0.0;
            // nrm 由矩形指向圆，而这里 A=矩形 B=鸟，方向正好
            qreal approach = 0;
            solveContact(ra, rb, pen, nrm, contact, eBirdBox, 0.30, &approach);
            // 鸟是主动撞击方，接近速度直接代表撞得多狠
            const qreal speed = -approach;
            if (speed > 60.0) {
                damageBlock(bk, speed * 0.16);
                if (speed > 420.0) {
                    playSfx(1);
                    emitParticles(contact, 6, 0, 1.0, 150,
                                  matStyle(bk.mat).base, 4.0);
                }
            }
        }
    }

    // -------- 鸟 vs 猪 --------
    for (Bird &bd : m_birds) {
        if (!bd.alive) continue;
        for (int i = 0; i < m_pigs.size(); ++i) {
            Pig &g = m_pigs[i];
            if (!g.alive) continue;
            QPointF nrm, contact;
            qreal pen = 0;
            if (!circlesOverlap(bd.pos, bd.r, g.pos, g.r, nrm, pen, contact)) continue;

            const QPointF rel = bd.vel - g.vel;
            const qreal speed = rel.manhattanLength();

            BodyRef ra, rb;
            ra.pos = &g.pos; ra.vel = &g.vel; ra.angVel = nullptr;
            ra.invMass = 0.32; ra.invI = 0.0;
            rb.pos = &bd.pos; rb.vel = &bd.vel; rb.angVel = nullptr;
            rb.invMass = 0.55; rb.invI = 0.0;
            qreal approach = 0;
            solveContact(ra, rb, pen, nrm, contact, eBirdPig, 0.35, &approach);

            // 撞击伤害：只看"鸟撞过来的相对速度"
            const qreal dmg = speed * 0.075;
            if (dmg < 1.0) continue;
            if (m_graceT > 0) continue;      // 开场保护期
            g.hurtT = 0.22;
            g.hp -= (int)dmg;
            if (dmg > 45.0) {
                emitParticles(contact, 7, 1, 0.8, 170, QColor(0xFF, 0xFF, 0xFF), 4.5);
                playSfx(1);
            }
            if (g.hp <= 0) killPig(i, speed);
        }
    }

    // -------- 积木 vs 猪 --------
    for (Block &bk : m_blocks) {
        if (!bk.alive) continue;
        for (int i = 0; i < m_pigs.size(); ++i) {
            Pig &g = m_pigs[i];
            if (!g.alive) continue;
            QPointF nrm, contact;
            qreal pen = 0;
            if (!circleVsBox(g.pos, g.r, bk.pos, bk.size, bk.angle, nrm, pen, contact))
                continue;

            BodyRef ra, rb;
            ra.pos = &bk.pos; ra.vel = &bk.vel; ra.angVel = &bk.angVel;
            ra.invMass = bk.invMass; ra.invI = bk.invI; ra.sleeping = &bk.sleeping;
            rb.pos = &g.pos; rb.vel = &g.vel; rb.angVel = nullptr;
            rb.invMass = 0.32; rb.invI = 0.0;
            qreal approach = 0;
            solveContact(ra, rb, pen, nrm, contact, 0.15, 0.5, &approach);

            // 同理：用接近速度，不用支撑冲量
            // (猪被砸死主要看积木砸过来的速度，猪自己走过去撞墙不算)
            const qreal dmg = (-approach - 60.0) * 0.10;
            if (dmg > 1.0 && m_graceT <= 0) {    // 开场保护期不结算
                g.hurtT = 0.22;
                g.hp -= (int)dmg;
                if (g.hp <= 0) killPig(i, approach);
            }
        }
    }

    // -------- 猪 vs 猪 --------
    for (int i = 0; i < m_pigs.size(); ++i) {
        if (!m_pigs[i].alive) continue;
        for (int k = i + 1; k < m_pigs.size(); ++k) {
            Pig &A = m_pigs[i];
            Pig &B = m_pigs[k];
            if (!B.alive) continue;
            QPointF nrm, contact;
            qreal pen = 0;
            if (!circlesOverlap(A.pos, A.r, B.pos, B.r, nrm, pen, contact)) continue;
            BodyRef ra, rb;
            ra.pos = &A.pos; ra.vel = &A.vel; ra.invMass = 0.32;
            rb.pos = &B.pos; rb.vel = &B.vel; rb.invMass = 0.32;
            solveContact(ra, rb, pen, nrm, contact, 0.2, 0.4);
        }
    }

    // -------- 鸟 vs 鸟 --------
    for (int i = 0; i < m_birds.size(); ++i) {
        if (!m_birds[i].alive) continue;
        for (int k = i + 1; k < m_birds.size(); ++k) {
            Bird &A = m_birds[i];
            Bird &B = m_birds[k];
            if (!B.alive) continue;
            QPointF nrm, contact;
            qreal pen = 0;
            if (!circlesOverlap(A.pos, A.r, B.pos, B.r, nrm, pen, contact)) continue;
            BodyRef ra, rb;
            ra.pos = &A.pos; ra.vel = &A.vel; ra.invMass = 0.55;
            rb.pos = &B.pos; rb.vel = &B.vel; rb.invMass = 0.55;
            solveContact(ra, rb, pen, nrm, contact, 0.3, 0.3);
        }
    }
}

bool game::worldSettled() const
{
    if (m_state == StAim) return false;

    // 判定"停下来没有"用统一的、略微放宽的阈值(World::STILL_VEL/World::STILL_ANG)。
    // 阈值原来卡得很紧(26/34/42), 物体落地后那点肉眼看不出的残余滑动会一直
    // 悬在阈值上方, 计时器被反复清零 —— 玩家感受就是"等它稳定要等好久"。
    for (const Bird &b : m_birds) {
        if (!b.alive || !b.flying) continue;
        if (b.vel.manhattanLength() > World::STILL_VEL) return false;
        if (b.pos.y() + b.r < World::GROUND_Y - 4.0) return false;   // 还在空中
    }
    for (const Block &b : m_blocks) {
        if (!b.alive || b.sleeping) continue;
        if (b.vel.manhattanLength() > World::STILL_VEL) return false;
        if (qAbs(b.angVel) > World::STILL_ANG) return false;
    }
    for (const Pig &g : m_pigs) {
        if (!g.alive) continue;
        if (g.vel.manhattanLength() > World::STILL_VEL) return false;
    }
    return true;
}

void game::handleRoundEnd(qreal dt)
{
    if (m_state == StWin) {
        m_winT += dt;
        return;
    }
    if (m_state == StLose) return;

    // 全部猪干掉 = 过关
    if (m_pigsTotal > 0 && pigsAliveCount() == 0) {
        m_state = StWin;
        m_winT  = 0;

        const int birdsLeft = m_queue.size();
        m_stars = (birdsLeft >= 2) ? 3 : (birdsLeft == 1 ? 2 : 1);
        addScore(10000 + birdsLeft * 5000);

        playSfx(5);
        for (int i = 0; i < 70; ++i) {
            Particle p;
            const qreal x = (qreal)((i * 2654435761u) % 1024);
            p.pos = QPointF(x, -20.0 - (i % 7) * 30.0);
            p.vel = QPointF(((i % 5) - 2) * 22.0, 90.0 + (i % 9) * 24.0);
            p.maxLife = p.life = 3.2;
            p.size = 7.0 + (i % 4) * 2.5;
            static const QColor cs[5] = { QColor(0xE0,0x3A,0x2B), QColor(0xF5,0xC5,0x11),
                                          QColor(0x2E,0x9B,0xE0), QColor(0x5F,0xC8,0x3E),
                                          QColor(0xFF,0xFF,0xFF) };
            p.color = cs[i % 5];
            p.type = 2;
            p.rot = (qreal)i;
            p.spin = ((i % 2) ? 1 : -1) * 3.0;
            m_particles.push_back(p);
        }
        return;
    }

    // 鸟打光了 + 全场静止 + 还有猪 = 失败
    if (m_state == StFly || m_state == StSettle) {
        // 正常路径: 全场静下来之后, 再稳住 STOP_TIME 就交还操作权
        if (worldSettled()) m_settleT += dt;
        else                m_settleT = 0;

        // 硬上限: 发射后最多等 MAX_TURN_WAIT 秒。
        // 塌方还没结束也照样把操作权交回玩家 —— 让它自己继续塌,
        // 玩家该打下一发就打, 不用干等着看它滚。
        const bool timedOut = (m_time - m_lastShotT) > World::MAX_TURN_WAIT;
        const bool settled  = (m_settleT > World::STOP_TIME);

        if (settled || timedOut) {
            if (m_queue.isEmpty()) {
                // 鸟打光了, 只剩失败一种可能。但必须真静下来才判,
                // 否则还在塌方就弹失败界面, 会把后续得分也算丢。
                if (settled) {
                    m_state = StLose;
                    playSfx(6);
                }
            } else {
                m_state = StAim;
                m_settleT = 0;
                m_pulling = false;
                m_pullPos = slingAnchor();
                // 收尾：把已经停下的鸟沉到地上
                for (Bird &b : m_birds) if (b.alive) b.flying = false;
            }
        }
    }
}

// ============================================================================
//  第六部分：绘制
// ============================================================================
qreal game::scaleF() const
{
    const qreal sx = width()  / World::W;
    const qreal sy = height() / World::H;
    return qMin(sx, sy);
}

QPointF game::viewOffset() const
{
    const qreal s = scaleF();
    return QPointF((width() - World::W * s) * 0.5,
                   (height() - World::H * s) * 0.5);
}

QPointF game::screenToWorld(const QPointF &s) const
{
    const qreal k = scaleF();
    const QPointF o = viewOffset();
    if (k < 1e-6) return s;
    return QPointF((s.x() - o.x()) / k, (s.y() - o.y()) / k);
}

QPointF game::rot2(const QPointF &v, qreal a) { return rotv(v, a); }
qreal   game::clampd(qreal v, qreal lo, qreal hi) { return clampv(v, lo, hi); }
QPointF game::crossS(qreal w, const QPointF &r) { return crossSV(w, r); }
qreal   game::cross(const QPointF &a, const QPointF &b) { return cross2(a, b); }

QColor game::lighten(const QColor &c, int d)
{
    return QColor(qMin(255, c.red() + d), qMin(255, c.green() + d),
                  qMin(255, c.blue() + d), c.alpha());
}

QColor game::darken(const QColor &c, int d)
{
    return QColor(qMax(0, c.red() - d), qMax(0, c.green() - d),
                  qMax(0, c.blue() - d), c.alpha());
}

void game::blockCorners(const Block &b, QPointF out[4]) const
{
    boxCornersOf(b.pos, b.size, b.angle, out);
}

// 取精灵图; 没有就按 render() 现场画一张存起来
QPixmap game::sprite(const QString &key, const QSize &sz,
                     const std::function<void(QPainter &)> &render) const
{
    auto it = m_sprites.find(key);
    if (it != m_sprites.end()) return it.value();

    QPixmap pm(sz);
    pm.fill(Qt::transparent);
    QPainter q(&pm);
    q.setRenderHint(QPainter::Antialiasing, true);
    render(q);
    q.end();

    m_sprites.insert(key, pm);
    return pm;
}

// 把一块积木的"样子"画在 (0,0) 为锚点的画布上（尺寸 = 2*overhang）
void game::renderBlockSprite(QPainter &q, const Block &b) const
{
    const qreal w = b.size.width(), h = b.size.height();
    const qreal ox = w * 0.5 + 2.0, oy = h * 0.5 + 2.0;
    q.save();
    q.translate(ox, oy);

    if (b.kind == 2) {                       // TNT
        const QRectF r(-w / 2, -h / 2, w, h);
        QLinearGradient g(r.topLeft(), r.bottomRight());
        g.setColorAt(0.0, QColor(0xD8, 0x3A, 0x2C));
        g.setColorAt(1.0, QColor(0x9A, 0x1F, 0x14));
        q.setPen(QPen(QColor(0x5A, 0x10, 0x0A), 2.0));
        q.setBrush(g);
        q.drawRoundedRect(r, 4, 4);
        q.setPen(Qt::NoPen);
        q.setBrush(QColor(0xF2, 0xC4, 0x1B));
        q.drawRect(QRectF(r.left() + 2, r.center().y() - 6, r.width() - 4, 12));
        QFont f = q.font();
        f.setBold(true);
        f.setPixelSize(15);
        q.setFont(f);
        q.setPen(QColor(0x5A, 0x10, 0x0A));
        q.drawText(QRectF(r.left(), r.center().y() - 9, r.width(), 18),
                   Qt::AlignCenter, QStringLiteral("TNT"));
        q.restore();          // 配对函数开头那次 save()
        return;
    }


    const MatStyle st = matStyle(b.mat);
    const QRectF r(-w / 2, -h / 2, w, h);

    QLinearGradient g(r.topLeft(), r.bottomLeft());
    g.setColorAt(0.0, st.light);
    g.setColorAt(0.42, st.base);
    g.setColorAt(1.0, st.dark);
    q.setPen(Qt::NoPen);
    q.setBrush(g);
    q.drawRect(r);

    if (b.mat == 0) {                        // 木纹
        q.setPen(QPen(QColor(0, 0, 0, 36), 1.2));
        // 注意: 这两个循环的步长是浮点, 上限来自尺寸。
        // 万一尺寸被内存踩坏变成 NaN/极大值, 浮点循环不会终止, 整个界面就冻死。
        // 所以显式钳住上下界和迭代次数, 尺寸异常时最多画 60 条就收工。
        const qreal top = qMax(r.top(), -2000.0), bot = qMin(r.bottom(), 4000.0);
        const qreal lft = qMax(r.left(), -2000.0), rgt = qMin(r.right(), 4000.0);
        int guard = 0;
        if (bot - top >= rgt - lft) {
            for (qreal y = top + 9.0; y < bot - 3.0 && guard < 60; y += 9.0, ++guard)
                q.drawLine(QPointF(lft + 2, y), QPointF(rgt - 2, y));
        } else {
            for (qreal x = lft + 11.0; x < rgt - 3.0 && guard < 60; x += 11.0, ++guard)
                q.drawLine(QPointF(x, top + 2), QPointF(x, bot - 2));
        }
        q.setBrush(QColor(0, 0, 0, 26));
        q.setPen(Qt::NoPen);
        const qreal sx = r.left() + 6.0 + std::fmod(std::fabs(b.seed) * 7.3, qMax(6.0, w - 14.0));
        const qreal sy = r.top()  + 6.0 + std::fmod(std::fabs(b.seed) * 3.1, qMax(6.0, h - 14.0));
        q.drawEllipse(QPointF(sx, sy), 3.2, 2.4);
    } else if (b.mat == 1) {                 // 石纹
        q.setPen(Qt::NoPen);
        q.setBrush(QColor(255, 255, 255, 30));
        q.drawPolygon(QPolygonF() << QPointF(r.left() + 3, r.top() + 4)
                                  << QPointF(r.left() + w * 0.55, r.top() + 3)
                                  << QPointF(r.left() + w * 0.44, r.top() + h * 0.40)
                                  << QPointF(r.left() + 4, r.top() + h * 0.34));
        q.setBrush(QColor(0, 0, 0, 34));
        q.drawPolygon(QPolygonF() << QPointF(r.right() - 4, r.bottom() - 5)
                                  << QPointF(r.left() + w * 0.42, r.bottom() - 4)
                                  << QPointF(r.left() + w * 0.55, r.bottom() - h * 0.42)
                                  << QPointF(r.right() - 5, r.bottom() - h * 0.48));
    } else {                                 // 玻璃高光
        q.setPen(Qt::NoPen);
        q.setBrush(QColor(255, 255, 255, 78));
        q.drawPolygon(QPolygonF() << QPointF(r.left() + 3, r.top() + 2)
                                  << QPointF(r.left() + w * 0.40, r.top() + 2)
                                  << QPointF(r.left() + 3, r.top() + h * 0.62));
    }

    if (b.kind == 1) {                       // 木箱加固条
        q.setPen(QPen(st.dark, 4.0));
        q.setBrush(Qt::NoBrush);
        q.drawLine(r.topLeft() + QPointF(3, 3), r.bottomRight() - QPointF(3, 3));
        q.drawLine(r.topRight() + QPointF(-3, 3), r.bottomLeft() + QPointF(3, -3));
    }

    q.setPen(QPen(st.dark, 2.0));
    q.setBrush(Qt::NoBrush);
    q.drawRect(r);
    q.setPen(QPen(QColor(255, 255, 255, 70), 2.0));
    q.drawLine(r.topLeft() + QPointF(2, 1.5), r.topRight() + QPointF(-2, 1.5));
    q.restore();
}

// 猪的"身体"(不含会眨的眼睛/受击白闪), 以自身中心为原点
void game::renderPigBody(QPainter &q, qreal r) const
{
    const QColor body(0x7C, 0xD0, 0x4F);
    const QColor dark(0x52, 0xA0, 0x2C);
    const QColor light(0xA8, 0xE8, 0x7C);

    q.setPen(Qt::NoPen);
    q.setBrush(dark);
    q.drawEllipse(QPointF(-r * 0.62, -r * 0.86), r * 0.26, r * 0.24);
    q.drawEllipse(QPointF( r * 0.62, -r * 0.86), r * 0.26, r * 0.24);
    q.setBrush(QColor(0x6B, 0xB8, 0x3F));
    q.drawEllipse(QPointF(-r * 0.62, -r * 0.86), r * 0.16, r * 0.14);
    q.drawEllipse(QPointF( r * 0.62, -r * 0.86), r * 0.16, r * 0.14);

    QRadialGradient bg(QPointF(-r * 0.30, -r * 0.34), r * 1.55);
    bg.setColorAt(0.00, light);
    bg.setColorAt(0.55, body);
    bg.setColorAt(1.00, dark);
    q.setBrush(bg);
    q.setPen(Qt::NoPen);
    q.drawEllipse(QPointF(0, 0), r, r);

    q.setBrush(QColor(255, 255, 255, 62));
    q.drawEllipse(QPointF(-r * 0.32, -r * 0.42), r * 0.34, r * 0.24);

    const QPointF nose(0, r * 0.16);
    q.setBrush(QColor(0x66, 0xB8, 0x3C));
    q.setPen(QPen(QColor(0x44, 0x8C, 0x24), r * 0.06));
    q.drawEllipse(nose, r * 0.40, r * 0.31);
    q.setPen(Qt::NoPen);
    q.setBrush(QColor(0x2F, 0x66, 0x18));
    q.drawEllipse(nose + QPointF(-r * 0.15, 0), r * 0.085, r * 0.11);
    q.drawEllipse(nose + QPointF( r * 0.15, 0), r * 0.085, r * 0.11);

    // 眼睛(睁着的那版, 眨眼时覆盖)
    const qreal eyeY = -r * 0.28, eyeX = r * 0.37;
    q.setBrush(QColor(255, 255, 255));
    q.setPen(QPen(QColor(0x3A, 0x7A, 0x1E), r * 0.055));
    q.drawEllipse(QPointF(-eyeX, eyeY), r * 0.235, r * 0.245);
    q.drawEllipse(QPointF( eyeX, eyeY), r * 0.235, r * 0.245);
    q.setPen(Qt::NoPen);
    q.setBrush(QColor(0x1A, 0x1A, 0x1A));
    q.drawEllipse(QPointF(-eyeX + r * 0.03, eyeY + r * 0.02), r * 0.105, r * 0.115);
    q.drawEllipse(QPointF( eyeX + r * 0.03, eyeY + r * 0.02), r * 0.105, r * 0.115);
    q.setBrush(QColor(255, 255, 255, 220));
    q.drawEllipse(QPointF(-eyeX + r * 0.09, eyeY - r * 0.06), r * 0.045, r * 0.045);
    q.drawEllipse(QPointF( eyeX + r * 0.09, eyeY - r * 0.06), r * 0.045, r * 0.045);

    // 嘴
    q.setPen(QPen(QColor(0x36, 0x6E, 0x1C), r * 0.075));
    q.setBrush(Qt::NoBrush);
    q.drawArc(QRectF(-r * 0.30, r * 0.44, r * 0.60, r * 0.42), 200 * 16, 140 * 16);
}

// 鸟的整体(翅膀摆到固定角度), 以自身中心为原点, 朝 +x
void game::renderBirdSprite(QPainter &q, qreal r, const QColor &body) const
{
    const QColor dark  = darken(body, 58);
    const QColor light = lighten(body, 62);

    q.setPen(Qt::NoPen);
    q.setBrush(dark);
    QPolygonF tail;
    tail << QPointF(-r * 0.80, -r * 0.30) << QPointF(-r * 1.72, -r * 0.66)
         << QPointF(-r * 1.60,  r * 0.06) << QPointF(-r * 0.82,  r * 0.30);
    q.drawPolygon(tail);

    QRadialGradient bg(QPointF(-r * 0.28, -r * 0.34), r * 1.62);
    bg.setColorAt(0.00, light);
    bg.setColorAt(0.52, body);
    bg.setColorAt(1.00, dark);
    q.setBrush(bg);
    q.setPen(Qt::NoPen);
    q.drawEllipse(QPointF(0, 0), r, r * 1.03);

    q.setBrush(QColor(0xFF, 0xF6, 0xDC, 220));
    q.drawPolygon(QPolygonF() << QPointF(-r * 0.42, r * 0.20) << QPointF(r * 0.42, r * 0.20)
                              << QPointF(r * 0.30, r * 0.88) << QPointF(-r * 0.30, r * 0.88));

    q.setBrush(dark);
    q.drawPolygon(QPolygonF() << QPointF(-r * 0.16, -r * 0.95)
                              << QPointF(r * 0.10, -r * 1.74)
                              << QPointF(r * 0.30, -r * 0.92));

    q.save();
    q.translate(-r * 0.06, -r * 0.02);
    q.rotate(-30.0);
    q.setBrush(dark);
    q.setPen(Qt::NoPen);
    q.drawEllipse(QPointF(0, 0), r * 0.60, r * 0.34);
    q.restore();

    const qreal eyeX = r * 0.36, eyeY = -r * 0.32;
    q.setBrush(QColor(255, 255, 255));
    q.setPen(QPen(QColor(0x2A, 0x2A, 0x2A), r * 0.06));
    q.drawEllipse(QPointF(eyeX, eyeY), r * 0.28, r * 0.30);
    q.setPen(Qt::NoPen);
    q.setBrush(QColor(0x14, 0x14, 0x14));
    q.drawEllipse(QPointF(eyeX + r * 0.07, eyeY + r * 0.02), r * 0.125, r * 0.145);
    q.setBrush(QColor(255, 255, 255, 225));
    q.drawEllipse(QPointF(eyeX + r * 0.13, eyeY - r * 0.08), r * 0.05, r * 0.05);

    q.setPen(QPen(QColor(0x1E, 0x14, 0x10), r * 0.16));
    q.drawLine(QPointF(eyeX - r * 0.30, eyeY - r * 0.56),
               QPointF(eyeX + r * 0.22, eyeY - r * 0.30));

    q.setPen(QPen(QColor(0xD8, 0x8A, 0x00), r * 0.05));
    QLinearGradient bk(QPointF(r * 0.55, -r * 0.12), QPointF(r * 1.5, r * 0.2));
    bk.setColorAt(0, QColor(0xFF, 0xC4, 0x22));
    bk.setColorAt(1, QColor(0xE8, 0x8A, 0x00));
    q.setBrush(bk);
    q.drawPolygon(QPolygonF() << QPointF(r * 0.62, -r * 0.30)
                              << QPointF(r * 1.46, r * 0.02)
                              << QPointF(r * 0.60, r * 0.30));
    q.setPen(QPen(QColor(0xB5, 0x6A, 0x00), r * 0.05));
    q.drawLine(QPointF(r * 0.64, r * 0.02), QPointF(r * 1.42, r * 0.03));
}

// 把每帧都不变的那一层(天空/山/树/草地)预先画进一张图。
// 换分辨率或换关时重建一次, 之后每帧只贴一张图。
void game::buildStaticCache()
{
    const qreal k = scaleF();
    if (k < 1e-6) return;

    const QSize px(qMax(1, (int)qRound(World::W * k)),
                   qMax(1, (int)qRound(World::H * k)));

    // 与屏幕同格式, 避免 drawImage 时做逐像素转换:
    //   linuxfb 板子 = 16bpp RGB565   → 用 RGB16
    //   桌面 32bpp                    → 用 RGB32
    QImage::Format f = QImage::Format_RGB32;
    if (QScreen *sc = QGuiApplication::primaryScreen()) {
        switch (sc->depth()) {
        case 16: f = QImage::Format_RGB16; break;
        case 24:
        case 32: f = QImage::Format_RGB32; break;
        default: f = QImage::Format_ARGB32_Premultiplied; break;
        }
    }

    m_bgCache = QImage(px, f);
    if (m_bgCache.isNull()) return;

    QPainter cp(&m_bgCache);
    cp.setRenderHint(QPainter::Antialiasing, true);
    cp.scale(k, k);
    cp.setClipRect(QRectF(0, 0, World::W, World::H));

    paintSky(cp);
    paintHills(cp);
    paintGround(cp);
}

void game::resizeEvent(QResizeEvent *e)
{
    QDialog::resizeEvent(e);
    buildStaticCache();
    rebuildSceneLayer();
    layoutHudButtons();     // 尺寸变了, 顶部按钮热区跟着重算
}

// 顶部三个 HUD 按钮(重来/音效/退出)的唯一计算入口。
// 构造函数和 resizeEvent() 都调它, 避免两处各写一份而算得不一样。
// 注意: 结算卡片的按钮另用 m_btnOvLeft/m_btnOvRight, 不要写回这里 —— 否则
// 过关后顶部"重来"会跑到屏幕中间去。
void game::layoutHudButtons()
{
    m_btnRestart = QRectF(width() - 330, 12, 96, 40);
    m_btnSound   = QRectF(width() - 226, 12, 104, 40);
    m_btnQuit    = QRectF(width() - 114, 12, 100, 40);
}

// 把"两次撞击之间不动的东西"全部烘进场景层:
// 背景 + 弹弓后腿 + 猪 + 积木 + 弹弓上待发的鸟。
// 之后每帧只把脏矩形那一小块从这张图拷回来, 其余像素一个都不碰。
void game::bakeSceneLayer(QPainter &q) const
{
    if (!m_bgCache.isNull()) {
        q.setRenderHint(QPainter::Antialiasing, false);
        q.drawImage(QRectF(0, 0, World::W, World::H), m_bgCache);
        q.setRenderHint(QPainter::Antialiasing, true);
    } else {
        paintSky(q); paintHills(q); paintGround(q);
    }
    paintSlingBack(q);
    // 把"这一帧不动的"积木烘进来。注意不能只看 sleeping 标志:
    // 被猪压着的横梁会一直被冲量唤醒, 永远睡不了, 但它实际上是不动的。
    for (const Block &b : m_blocks)
        if (b.alive && blockIsStill(b)) drawBlockShape(q, b);
    paintPigs(q);
    // 弹弓上待发的那只鸟在静止时也不动, 一起烘进来
    if (m_state == StAim && !m_queue.isEmpty() && !m_pulling)
        drawBird(q, slingAnchor(), World::BIRD_R, m_queue.first(), 0.0, false, 0.0);
}

// 重建整个场景层(关卡切换/布局变化时调用)
void game::rebuildSceneLayer()
{
    const qreal k = scaleF();
    if (k < 1e-6) return;

    const QSize px(qMax(1, (int)qRound(World::W * k)),
                   qMax(1, (int)qRound(World::H * k)));
    QImage::Format f = m_bgCache.isNull() ? QImage::Format_RGB32 : m_bgCache.format();
    m_sceneCache = QImage(px, f);
    m_sceneCache.fill(Qt::black);

    QPainter q(&m_sceneCache);
    q.setRenderHint(QPainter::Antialiasing, true);
    q.scale(k, k);
    bakeSceneLayer(q);
    q.end();

    // 记下这一版场景层烘了哪些积木, 下次有变化才重烘
    m_bakedStill.resize(m_blocks.size());
    for (int i = 0; i < m_blocks.size(); ++i)
        m_bakedStill[i] = (m_blocks[i].alive && blockIsStill(m_blocks[i]));

    m_dirtyAll = true;
}


// 把需要重画的那块区域从场景缓存拷回画面。
// wantPx 是本次 paintEvent 真正需要重画的屏幕矩形(Qt 通过 ev->rect() 告诉我们),
// 我们只恢复这一块, 别的像素原样保留 —— 这就是省时间的地方。
void game::restoreDirty(QPainter &p, const QRect &wantPx)
{
    const qreal k = scaleF();

    if (m_sceneCache.isNull() || k < 1e-6) return;

    QRect d;
    if (m_dirtyAll) {
        d = QRect(0, 0, (int)World::W, (int)World::H);
    } else if (!wantPx.isNull()) {
        // 屏幕矩形 -> 世界坐标
        const QPointF vofs = viewOffset();
        const QRectF wf((wantPx.left() - vofs.x()) / k, (wantPx.top() - vofs.y()) / k,
                        wantPx.width() / k, wantPx.height() / k);
        d = QRect((int)std::floor(wf.left()), (int)std::floor(wf.top()),
                  (int)std::ceil(wf.width()) + 1, (int)std::ceil(wf.height()) + 1);
    } else {
        return;   // 没有需要重画的区域
    }
    d = d.intersected(QRect(0, 0, (int)World::W, (int)World::H));
    if (d.isNull()) return;

    const QRect src((int)(d.left() * k), (int)(d.top() * k),
                    qMax(1, (int)(d.width() * k)), qMax(1, (int)(d.height() * k)));
    const QPointF vofs = viewOffset();
    const QRect dst((int)(vofs.x() + d.left() * k), (int)(vofs.y() + d.top() * k),
                    src.width(), src.height());

    // 用 Source 直接覆盖脏区, 不跟旧像素混合, 也就不需要先清整屏。
    // (之前这里先做了一次全屏 fillRect, 1MB 的写入把省下来的时间又赔回去了)
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(dst, m_sceneCache, src);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    m_dirtyAll = false;
}

// 收集本帧"活动物体"的世界坐标包围盒。
// 这就是发射区(拉弓的鸟+皮筋)和撞击区(飞行中的鸟/碎块/粒子) ——
// 只有这些需要每帧重画, 其余全是静图。
void game::collectActiveBox()
{
    auto addC = [this](const QPointF &c, qreal r) {
        const QRectF b(c.x() - r, c.y() - r, r * 2, r * 2);
        m_actBox = m_actBox.isNull() ? b.toAlignedRect() : m_actBox.united(b.toAlignedRect());
    };

    // 拉弓中的小鸟 + 弹弓皮筋
    if (m_state == StAim && !m_queue.isEmpty() && m_pulling) {
        addC(m_pullPos, World::BIRD_R + 10.0);
        addC(slingAnchor(), World::MAX_PULL + 16.0);
        // 预测轨迹往右上方铺开, 用锚点+拉伸方向估一块
        const QPointF pull = slingAnchor() - m_pullPos;
        addC(slingAnchor() + QPointF(-pull.x() * 2.2, pull.y() * 2.2),
             World::MAX_PULL * 1.6);
    }
    // 飞行中的鸟(含拖尾)
    for (const Bird &b : m_birds) {
        if (!b.alive) continue;
        addC(b.pos, b.r * 2.6 + 6.0);
        if (!b.trail.isEmpty()) {
            addC(b.trail.first(), 12.0);
            addC(b.trail.last(), 12.0);
        }
    }
    // 碎块
    for (const Plank &pl : m_debris)
        addC(pl.pos, qMax(pl.size.width(), pl.size.height()) + 8.0);
    // 粒子
    for (const Particle &pt : m_particles)
        addC(pt.pos, pt.size * 3.0 + 6.0);
    // 正在动的积木(塌方中)
    for (const Block &b : m_blocks) {
        if (!b.alive || blockIsStill(b)) continue;
        addC(b.pos, qMax(b.size.width(), b.size.height()) * 0.75 + 8.0);
    }
    // 猪被撞得在弹/受击闪白
    for (const Pig &g : m_pigs) {
        if (!g.alive || (g.vel.manhattanLength() < 2.0 && g.hurtT <= 0 && g.squash > 0.999))
            continue;
        addC(g.pos, g.r * 1.6 + 6.0);
    }
    // 结算卡片入场动画
    if ((m_state == StWin || m_state == StLose) && m_winT < 0.5) {
        m_actBox = QRect(0, 0, (int)World::W, (int)World::H);
    }
}

void game::paintEvent(QPaintEvent *ev)
{
    QPainter p(this);

    // 本帧哪些活动物体需要重画? 先收集它们的世界坐标包围盒。
    // 这就是"发射区"(拉弓的小鸟+皮筋)和"撞击区"(飞行的鸟/碎块/粒子),
    // 其余东西都是静图, 一个像素都不用动。
    // 活动范围 = 上一帧的范围 ∪ 当前帧的范围。
    // 少算了上一帧, 物体移动后它原来的位置就会留下残影(旧的鸟/碎块印在画面上)。
    const QRect prevBox = m_actBox;
    m_actBox = QRect();
    collectActiveBox();
    if (!prevBox.isNull())
        m_actBox = m_actBox.isNull() ? prevBox : m_actBox.united(prevBox);
    m_haveActive = !m_actBox.isNull();

    // 需要重画的屏幕区域。
    // 注意: 不能用 ev->rect()。我们是用无参 update() 触发的重绘, Qt 在这种情况下
    // 一律给整屏 1024x600, 拿它做基础就等于每帧全屏重画, 区域重绘彻底失效。
    // 这里直接以"活动物体包围盒"为准。
    const qreal k = scaleF();
    const QPointF vofs = viewOffset();
    QRect want;
    if (m_dirtyAll || !m_haveActive) {
        want = rect();                              // 首帧/重建/无活动物体: 整屏
    } else {
        want = QRect((int)std::floor(vofs.x() + m_actBox.left() * k),
                     (int)std::floor(vofs.y() + m_actBox.top()  * k),
                     (int)std::ceil(m_actBox.width()  * k) + 2,
                     (int)std::ceil(m_actBox.height() * k) + 2);
        // 只有 Qt 明确要求重画更大范围时才扩大(例如窗口被遮挡后重新露出)
        if (ev && !ev->rect().isEmpty() && ev->rect() != rect())
            want = want.united(ev->rect());
    }
    want = want.intersected(rect());
    if (want.isEmpty()) want = rect();

    // ---- 1. 恢复静态部分(只动这块区域) ----
    restoreDirty(p, want);

    // 只在这块区域内绘制, 越界的笔触直接丢弃
    p.setClipRect(want);

    p.save();
    p.translate(viewOffset());
    p.scale(scaleF(), scaleF());
    p.setRenderHint(QPainter::Antialiasing, true);

    // ---- 2. 只画"活的"东西, 并顺手把它们的范围标成下一帧的脏区 ----
    if (m_state == StAim && m_pulling) {
        // 拉弓中: 鸟跟着手指走, 弹弓皮筋要重画
    }
    if (m_state == StAim && !m_queue.isEmpty() && m_pulling) {
        const QPointF pos = slingBirdPos();
        const qreal tension = clampv(euclidLen(pos - slingAnchor()) / World::MAX_PULL, 0, 1);
        const QPointF jit(std::sin(m_time * 42.0) * tension * 1.4,
                          std::cos(m_time * 37.0) * tension * 1.4);
        if (m_pulling) {
            drawBand(p, pos, false);
            drawBand(p, pos, true);
        }
        drawBird(p, pos + jit, World::BIRD_R, m_queue.first(),
                 m_pulling ? std::atan2(slingAnchor().y() - pos.y(),
                                        slingAnchor().x() - pos.x()) : 0.0,
                 false, m_time);
    }

    // 在动的积木(塌方中)实时画; 不动的已经在场景层里了
    for (const Block &b : m_blocks) {
        if (!b.alive || blockIsStill(b)) continue;
        drawBlockShape(p, b);
    }

    // 已发射的鸟 + 轨迹
    p.setBrush(Qt::NoBrush);
    for (const Bird &b : m_birds) {
        if (!b.alive) continue;
        const int n = b.trail.size();
        if (n >= 3) {
            for (int seg = 0; seg < 3; ++seg) {
                const int i0 = n * seg / 3, i1 = n * (seg + 1) / 3;
                if (i1 - i0 < 2) continue;
                QPainterPath path;
                path.moveTo(b.trail[i0]);
                for (int i = i0 + 1; i < i1; ++i) path.lineTo(b.trail[i]);
                QPen pen(QColor(0xFF, 0xFF, 0xFF, 38 + seg * 42));
                pen.setWidthF(1.6 + seg * 1.5);
                pen.setCapStyle(Qt::RoundCap);
                pen.setJoinStyle(Qt::RoundJoin);
                p.setPen(pen);
                p.drawPath(path);
            }
        }
        drawBird(p, b.pos, b.r, b.color, b.rot, b.flying, b.flapT);
    }

    // 被撞飞的碎块
    for (const Plank &pl : m_debris) {
        drawPlank(p, pl);
    }

    // 粒子
    for (const Particle &pt : m_particles) {
        const qreal t = clampv(pt.life / qMax(0.01, pt.maxLife), 0.0, 1.0);
        QColor c = pt.color;
        c.setAlpha((int)(c.alpha() * clampv(t * 1.6, 0.0, 1.0)));
        p.save();
        p.translate(pt.pos);
        p.rotate(qRadiansToDegrees(pt.rot));
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        if (pt.type == 1) {
            p.setBrush(QColor(c.red(), c.green(), c.blue(), c.alpha() / 2));
            p.drawEllipse(QPointF(0, 0), pt.size * (2.0 - t) * 1.7, pt.size * (2.0 - t) * 1.7);
            p.setBrush(c);
            p.drawEllipse(QPointF(0, 0), pt.size * (2.0 - t), pt.size * (2.0 - t));
        } else if (pt.type == 2) {
            p.drawRect(QRectF(-pt.size * 0.5, -pt.size * 0.28, pt.size, pt.size * 0.56));
        } else if (pt.type == 3) {
            p.drawEllipse(QPointF(0, 0), pt.size * 0.9, pt.size * 0.32);
        } else if (pt.type == 5) {
            p.setBrush(QColor(c.red(), c.green(), c.blue(), c.alpha() / 3));
            p.drawEllipse(QPointF(0, 0), pt.size, pt.size * 0.82);
        } else {
            p.drawRect(QRectF(-pt.size * 0.5, -pt.size * 0.5, pt.size, pt.size));
        }
        p.restore();
    }

    // 瞄准辅助线画在弹弓附近, 一并标脏
    paintAimUi(p);

    p.restore();

    // ---- 3. HUD / 结算卡(自带缓存, 便宜) ----
    paintHud(p);
    paintOverlay(p);

#ifdef GAME_PERF
    qDebug("[perf] dirty=%d,%d %dx%d", m_dirty.x(), m_dirty.y(), m_dirty.width(), m_dirty.height());
#endif
}

void game::drawThickLine(QPainter &p, const QPointF &a, const QPointF &b,
                         const QColor &c, qreal w) const
{
    QPen pen(c);
    pen.setWidthF(w);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawLine(a, b);
}

// ---------------------------------------------------------------- 天空 -------
void game::paintSky(QPainter &p) const
{
    QLinearGradient g(0, 0, 0, World::GROUND_Y);
    g.setColorAt(0.00, QColor(0x3C, 0xB0, 0xE6));
    g.setColorAt(0.42, QColor(0x8B, 0xD6, 0xF2));
    g.setColorAt(1.00, QColor(0xE2, 0xF5, 0xFD));
    p.fillRect(QRectF(-60, -60, World::W + 120, World::GROUND_Y + 60), g);

    // 太阳 + 光晕
    const QPointF sun(World::W - 148, 92);
    QRadialGradient halo(sun, 190);
    halo.setColorAt(0.00, QColor(255, 250, 200, 175));
    halo.setColorAt(0.35, QColor(255, 243, 150, 62));
    halo.setColorAt(1.00, QColor(255, 243, 150, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(halo);
    p.drawEllipse(sun, 190.0, 190.0);
    p.setBrush(QColor(0xFF, 0xF7, 0xB0));
    p.drawEllipse(sun, 36.0, 36.0);
    p.setBrush(QColor(255, 255, 255, 90));
    p.drawEllipse(sun + QPointF(-11, -11), 15.0, 15.0);

    // 云
    struct Cloud { qreal x, y, s; };
    static const Cloud CL[6] = {
        { 130,  96, 1.00 }, { 432,  58, 0.70 }, { 700, 126, 0.88 },
        { 268, 208, 0.52 }, { 902,  66, 0.62 }, { 560, 186, 0.44 }
    };
    for (int i = 0; i < 6; ++i) {
        const Cloud &c = CL[i];
        const qreal s = c.s;
        QLinearGradient cg(0, c.y - 48 * s, 0, c.y + 40 * s);
        cg.setColorAt(0, QColor(255, 255, 255, 242));
        cg.setColorAt(1, QColor(0xD2, 0xEB, 0xFA, 205));
        QPainterPath cp;
        cp.setFillRule(Qt::WindingFill);
        cp.addEllipse(QPointF(c.x - 54 * s, c.y +  8 * s), 40 * s, 27 * s);
        cp.addEllipse(QPointF(c.x + 54 * s, c.y + 10 * s), 44 * s, 30 * s);
        cp.addEllipse(QPointF(c.x - 18 * s, c.y - 12 * s), 41 * s, 33 * s);
        cp.addEllipse(QPointF(c.x + 22 * s, c.y - 16 * s), 47 * s, 37 * s);
        cp.addEllipse(QPointF(c.x,        c.y + 14 * s), 80 * s, 26 * s);
        p.setBrush(cg);
        p.setPen(Qt::NoPen);
        p.drawPath(cp);
    }
}

// ---------------------------------------------------------------- 远景 -------
void game::paintHills(QPainter &p) const
{
    // 远山
    QPainterPath far;
    far.setFillRule(Qt::WindingFill);
    far.addEllipse(QPointF(-30,  640), 420, 190);
    far.addEllipse(QPointF(300,  660), 460, 230);
    far.addEllipse(QPointF(660,  650), 430, 200);
    far.addEllipse(QPointF(1000, 665), 420, 235);
    QLinearGradient fg(0, 380, 0, World::GROUND_Y + 20);
    fg.setColorAt(0, QColor(0xAF, 0xE0, 0x9A));
    fg.setColorAt(1, QColor(0x87, 0xC9, 0x6C));
    p.setPen(Qt::NoPen);
    p.setBrush(fg);
    p.drawPath(far);

    // 近山
    QPainterPath near;
    near.setFillRule(Qt::WindingFill);
    near.addEllipse(QPointF(90,  690), 400, 200);
    near.addEllipse(QPointF(470, 710), 430, 235);
    near.addEllipse(QPointF(850, 700), 420, 210);
    QLinearGradient ng(0, 430, 0, World::GROUND_Y + 20);
    ng.setColorAt(0, QColor(0x86, 0xCC, 0x62));
    ng.setColorAt(1, QColor(0x62, 0xAE, 0x45));
    p.setBrush(ng);
    p.drawPath(near);

    // 背景小树
    struct Tree { qreal x, y, s; };
    static const Tree TR[7] = {
        {  62, 470, 0.90 }, { 118, 486, 0.68 }, { 372, 462, 1.00 },
        { 418, 480, 0.74 }, { 566, 474, 0.86 }, { 928, 466, 0.96 },
        { 972, 484, 0.72 }
    };
    for (int i = 0; i < 7; ++i) {
        const Tree &t = TR[i];
        const qreal s = t.s;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x7A, 0x50, 0x28));
        p.drawRect(QRectF(t.x - 4 * s, t.y, 8 * s, 44 * s));
        QPainterPath crown;
        crown.setFillRule(Qt::WindingFill);
        crown.addEllipse(QPointF(t.x,           t.y - 22 * s), 30 * s, 26 * s);
        crown.addEllipse(QPointF(t.x - 18 * s,  t.y -  4 * s), 23 * s, 21 * s);
        crown.addEllipse(QPointF(t.x + 18 * s,  t.y -  4 * s), 23 * s, 21 * s);
        crown.addEllipse(QPointF(t.x,           t.y +  4 * s), 25 * s, 18 * s);
        QLinearGradient tg(0, t.y - 50 * s, 0, t.y + 18 * s);
        tg.setColorAt(0, QColor(0x66, 0xB8, 0x45));
        tg.setColorAt(1, QColor(0x3E, 0x8C, 0x2C));
        p.setBrush(tg);
        p.drawPath(crown);
    }
}

// ---------------------------------------------------------------- 地面 -------
void game::paintGround(QPainter &p) const
{
    const qreal gy = World::GROUND_Y;

    QLinearGradient g(0, gy - 8, 0, World::H);
    g.setColorAt(0.00, QColor(0x76, 0xCC, 0x48));
    g.setColorAt(0.14, QColor(0x4F, 0xA0, 0x30));
    g.setColorAt(0.20, QColor(0x9A, 0x69, 0x38));
    g.setColorAt(0.55, QColor(0x7B, 0x4F, 0x27));
    g.setColorAt(1.00, QColor(0x53, 0x35, 0x1A));
    p.setPen(Qt::NoPen);
    p.fillRect(QRectF(-60, gy, World::W + 120, World::H - gy + 60), g);

    // 草叶
    p.setBrush(QColor(0x8D, 0xDD, 0x59));
    for (int i = 0; i < 70; ++i) {
        const qreal x = i * 15.2 + 5.0;
        const qreal h = 6.0 + (qreal)((i * 37) % 7);
        QPolygonF t;
        t << QPointF(x - 4.5, gy + 4) << QPointF(x, gy - h) << QPointF(x + 4.5, gy + 4);
        p.drawPolygon(t);
    }

    // 土壤里的石子
    p.setBrush(QColor(0, 0, 0, 34));
    for (int i = 0; i < 30; ++i) {
        const qreal x = (qreal)((i * 173) % 1010) + 8.0;
        const qreal y = gy + 26 + (qreal)((i * 67) % 62);
        p.drawEllipse(QPointF(x, y), 3.0 + (i % 3), 2.0 + (i % 2));
    }
    p.setBrush(QColor(255, 255, 255, 26));
    for (int i = 0; i < 18; ++i) {
        const qreal x = (qreal)((i * 291) % 1000) + 14.0;
        const qreal y = gy + 30 + (qreal)((i * 97) % 56);
        p.drawEllipse(QPointF(x, y), 4.0 + (i % 3), 2.5);
    }

    // 地面高光线
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, 82), 2.0));
    p.drawLine(QPointF(-60, gy + 1), QPointF(World::W + 60, gy + 1));
}

// ---------------------------------------------------------------- 弹弓 -------
void game::drawBand(QPainter &p, const QPointF &to, bool front) const
{
    const QPointF anchor = slingAnchor();
    const QPointF tipL(anchor.x() - 15.0, anchor.y() - 7.0);
    const QPointF tipR(anchor.x() + 15.0, anchor.y() - 11.0);
    const QColor bc(0x53, 0x2C, 0x14);

    if (front) {
        drawThickLine(p, to, tipR, bc, 9.0);
        // 皮兜
        QPointF d = unit(tipR - to);
        if (d.isNull()) d = QPointF(1, 0);
        const QPointF n(-d.y(), d.x());
        QPolygonF pouch;
        pouch << to + n * 13.0 << to + d * 7.0 + n * 11.0
              << to + d * 7.0 - n * 11.0 << to - n * 13.0;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x3A, 0x1E, 0x0D));
        p.drawPolygon(pouch);
    } else {
        drawThickLine(p, tipL, to, bc, 9.0);
    }
}

void game::paintSlingBack(QPainter &p) const
{
    const QPointF a = slingAnchor();

    // 后叉（先画，让鸟挡住它一半）
    QPen pen(QColor(0x6B, 0x44, 0x22));
    pen.setWidthF(13.0);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(a.x(), World::GROUND_Y + 4), QPointF(a.x() - 2.0, a.y() + 46.0));
    p.drawLine(QPointF(a.x() - 2.0, a.y() + 46.0), QPointF(a.x() - 15.0, a.y() - 8.0));

    // 皮筋（后）
    if (m_state == StAim && m_queue.size() > 0)
        drawBand(p, slingBirdPos(), false);
}

void game::paintSlingFront(QPainter &p) const
{
    const QPointF a = slingAnchor();

    // 前叉
    QPen pen(QColor(0x8A, 0x5A, 0x2E));
    pen.setWidthF(13.0);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(a.x() - 2.0, a.y() + 46.0), QPointF(a.x() + 15.0, a.y() - 12.0));

    // 叉口高光
    pen.setColor(QColor(0xB8, 0x86, 0x50));
    pen.setWidthF(4.0);
    p.setPen(pen);
    p.drawLine(QPointF(a.x() - 4.0, a.y() + 40.0), QPointF(a.x() + 12.0, a.y() - 6.0));

    // 皮筋（前）
    if (m_state == StAim && m_queue.size() > 0)
        drawBand(p, slingBirdPos(), true);
}

// ---------------------------------------------------------------- 积木 -------
void game::drawBlockShape(QPainter &p, const Block &b) const
{
    // 外形(渐变+纹理+描边)与姿态无关, 缓存成精灵; 每帧只剩一次旋转贴图
    const int w = qMax(4, (int)qRound(b.size.width()));
    const int h = qMax(4, (int)qRound(b.size.height()));
    const QSize sz(w + 4, h + 4);
    const QString key = QStringLiteral("blk|%1|%2|%3|%4|%5")
            .arg(b.mat).arg(b.kind).arg(w).arg(h)
            .arg((int)(std::fabs(b.seed) * 13.0) % 97);
    const QPixmap pm = sprite(key, sz, [&](QPainter &q) { renderBlockSprite(q, b); });

    p.save();
    p.translate(b.pos);
    p.rotate(qRadiansToDegrees(b.angle));
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawPixmap(QPointF(-sz.width() * 0.5, -sz.height() * 0.5), pm);

    // 受击闪白是瞬时的, 不进缓存
    if (b.hurtT > 0) {
        const qreal w2 = b.size.width() * 0.5, h2 = b.size.height() * 0.5;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, (int)clampv(b.hurtT * 900.0, 0, 180)));
        p.drawRect(QRectF(-w2, -h2, w2 * 2, h2 * 2));
    }
    p.restore();

    // 裂纹（血量低时）
    const int hpMax = (b.mat == 0) ? 68 : (b.mat == 1 ? 190 : 26);
    if (b.alive && b.hp < hpMax * 0.6) {
        p.save();
        p.translate(b.pos);
        p.rotate(qRadiansToDegrees(b.angle));
        p.setPen(QPen(QColor(0, 0, 0, 110), 1.6));
        p.setBrush(Qt::NoBrush);
        const qreal w = b.size.width(), h = b.size.height();
        p.drawLine(QPointF(-w * 0.28, -h * 0.46), QPointF(-w * 0.06, 0.0));
        p.drawLine(QPointF(-w * 0.06, 0.0), QPointF(-w * 0.22, h * 0.46));
        if (b.hp < hpMax * 0.3) {
            p.drawLine(QPointF(w * 0.30, -h * 0.44), QPointF(w * 0.08, h * 0.06));
            p.drawLine(QPointF(w * 0.08, h * 0.06), QPointF(w * 0.26, h * 0.44));
        }
        p.restore();
    }
}

void game::paintBlocks(QPainter &p) const
{
    for (const Block &b : m_blocks) {
        if (!b.alive) continue;
        drawBlockShape(p, b);
    }
}

void game::drawPlank(QPainter &p, const Plank &pl) const
{
    const MatStyle st = matStyle(pl.mat);
    p.save();
    p.translate(pl.pos);
    p.rotate(qRadiansToDegrees(pl.angle));
    const QRectF r(-pl.size.width() / 2, -pl.size.height() / 2,
                   pl.size.width(), pl.size.height());
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    g.setColorAt(0, st.light);
    g.setColorAt(1, st.dark);
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    if (pl.mat == 2) p.drawPolygon(QPolygonF() << r.topLeft()
                        << QPointF(r.right(), r.top() + r.height() * 0.28)
                        << QPointF(r.left() + r.width() * 0.7, r.bottom())
                        << QPointF(r.left(), r.bottom() - r.height() * 0.32));
    else             p.drawRect(r);
    p.setPen(QPen(st.dark, 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);
    p.restore();
}

void game::paintDebris(QPainter &p) const
{
    for (const Plank &pl : m_debris) {
        p.save();
        QColor c = matStyle(pl.mat).base;
        c.setAlpha((int)clampv(pl.life * 200.0, 0, 255));
        p.setOpacity(clampv(pl.life / 0.9, 0.0, 1.0));
        drawPlank(p, pl);
        p.restore();
        Q_UNUSED(c)
    }
}

// ---------------------------------------------------------------- 猪 ---------
void game::drawPig(QPainter &p, const QPointF &c, qreal r, qreal hurt,
                   qreal blink, qreal squash) const
{
    // 眨眼周期决定用哪张精灵(睁眼/闭眼), 与位置无关 → 缓存
    const qreal cyc = std::fmod(blink, 3.4);
    const bool blinkNow = (cyc > 3.15 && cyc < 3.32);

    const int ri = qMax(4, (int)qRound(r));
    // 猪的耳朵/眼睛外扩到 ±r*0.95, 画布留 2px 余量并多留 2px 给缩放
    const QSize sz(ri * 2 + 8, ri * 2 + 8);
    const QString key = QStringLiteral("pig|%1|%2").arg(ri).arg(blinkNow ? 1 : 0);

    const QPixmap pm = sprite(key, sz, [&](QPainter &q) {
        const qreal off = sz.width() * 0.5;
        q.translate(off, off);
        renderPigBody(q, r);
        if (blinkNow) {                      // 闭眼时用眼皮盖掉睁着的眼睛
            const qreal eyeY = -r * 0.28, eyeX = r * 0.37;
            q.setPen(QPen(QColor(0x3A, 0x7A, 0x1E), r * 0.075));
            q.setBrush(QColor(0xCB, 0xEA, 0xB6));
            q.drawEllipse(QPointF(-eyeX, eyeY), r * 0.235, r * 0.245);
            q.drawEllipse(QPointF( eyeX, eyeY), r * 0.235, r * 0.245);
            q.setPen(QPen(QColor(0x2F, 0x66, 0x18), r * 0.07));
            q.drawLine(QPointF(-eyeX - r * 0.18, eyeY), QPointF(-eyeX + r * 0.18, eyeY));
            q.drawLine(QPointF( eyeX - r * 0.18, eyeY), QPointF( eyeX + r * 0.18, eyeY));
        }
    });

    p.save();
    p.translate(c);
    p.scale(1.0 / qMax(0.55, squash), squash);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawPixmap(QPointF(-sz.width() * 0.5, -sz.height() * 0.5), pm);

    // 受击闪白是瞬时的, 不进缓存
    if (hurt > 0) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, (int)clampv(hurt * 720.0, 0, 190)));
        p.drawEllipse(QPointF(0, 0), r, r);
    }
    p.restore();
}

void game::paintPigs(QPainter &p) const
{
    for (const Pig &g : m_pigs) {
        if (!g.alive) continue;
        drawPig(p, g.pos, g.r, g.hurtT, g.blinkPhase, g.squash);
    }
}

// ---------------------------------------------------------------- 鸟 ---------
void game::drawBird(QPainter &p, const QPointF &c, qreal r, const QColor &body,
                    qreal rot, bool flying, qreal flap) const
{
    p.save();
    p.translate(c);
    p.rotate(qRadiansToDegrees(rot));

    const QColor dark  = darken(body, 58);
    const QColor light = lighten(body, 62);

    // 尾巴
    p.setPen(Qt::NoPen);
    p.setBrush(dark);
    QPolygonF tail;
    tail << QPointF(-r * 0.80, -r * 0.30) << QPointF(-r * 1.72, -r * 0.66)
         << QPointF(-r * 1.60,  r * 0.06) << QPointF(-r * 0.82,  r * 0.30);
    p.drawPolygon(tail);

    // 身体
    QRadialGradient bg(QPointF(-r * 0.28, -r * 0.34), r * 1.62);
    bg.setColorAt(0.00, light);
    bg.setColorAt(0.52, body);
    bg.setColorAt(1.00, dark);
    p.setBrush(bg);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(0, 0), r, r * 1.03);

    // 肚皮
    p.setBrush(QColor(0xFF, 0xF6, 0xDC, 220));
    QPolygonF belly;
    belly << QPointF(-r * 0.42, r * 0.20) << QPointF(r * 0.42, r * 0.20)
          << QPointF(r * 0.30, r * 0.88) << QPointF(-r * 0.30, r * 0.88);
    p.drawPolygon(belly);

    // 头顶呆毛（飞的时候会被吹直）
    p.setBrush(dark);
    const qreal tuff = flying ? 1.0 : 0.55;
    QPolygonF tuft;
    tuft << QPointF(-r * 0.16, -r * 0.95)
         << QPointF(r * 0.10, -r * (1.62 + 0.25 * tuff))
         << QPointF(r * 0.30, -r * 0.92);
    p.drawPolygon(tuft);

    // 翅膀
    p.setBrush(dark);
    const qreal wf = std::sin(flap * 17.0) * (flying ? 1.0 : 0.16);
    p.save();
    p.translate(-r * 0.06, -r * 0.02);
    p.rotate(-30.0 + wf * 34.0);
    p.drawEllipse(QPointF(0, 0), r * 0.60, r * 0.34);
    p.restore();

    // 眼睛
    const qreal eyeX = r * 0.36, eyeY = -r * 0.32;
    p.setBrush(QColor(255, 255, 255));
    p.setPen(QPen(QColor(0x2A, 0x2A, 0x2A), r * 0.06));
    p.drawEllipse(QPointF(eyeX, eyeY), r * 0.28, r * 0.30);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x14, 0x14, 0x14));
    p.drawEllipse(QPointF(eyeX + r * 0.07, eyeY + r * 0.02), r * 0.125, r * 0.145);
    p.setBrush(QColor(255, 255, 255, 225));
    p.drawEllipse(QPointF(eyeX + r * 0.13, eyeY - r * 0.08), r * 0.05, r * 0.05);

    // 愤怒的眉毛
    p.setPen(QPen(QColor(0x1E, 0x14, 0x10), r * 0.16));
    p.drawLine(QPointF(eyeX - r * 0.30, eyeY - r * 0.56),
               QPointF(eyeX + r * 0.22, eyeY - r * 0.30));

    // 喙
    p.setPen(QPen(QColor(0xD8, 0x8A, 0x00), r * 0.05));
    QLinearGradient bk(QPointF(r * 0.55, -r * 0.12), QPointF(r * 1.5, r * 0.2));
    bk.setColorAt(0, QColor(0xFF, 0xC4, 0x22));
    bk.setColorAt(1, QColor(0xE8, 0x8A, 0x00));
    p.setBrush(bk);
    QPolygonF beak;
    beak << QPointF(r * 0.62, -r * 0.30) << QPointF(r * 1.46, r * 0.02)
         << QPointF(r * 0.60, r * 0.30);
    p.drawPolygon(beak);
    p.setPen(QPen(QColor(0xB5, 0x6A, 0x00), r * 0.05));
    p.drawLine(QPointF(r * 0.64, r * 0.02), QPointF(r * 1.42, r * 0.03));

    p.restore();
}

void game::paintBirds(QPainter &p) const
{
    // 已发射的鸟
    for (const Bird &b : m_birds) {
        if (!b.alive) continue;
        drawBird(p, b.pos, b.r, b.color, b.rot, b.flying, b.flapT);
    }
    // 弹弓上待发的鸟
    if (m_state == StAim && !m_queue.isEmpty()) {
        const QPointF pos = slingBirdPos();
        const qreal tension = m_pulling
            ? clampv(euclidLen(pos - slingAnchor()) / World::MAX_PULL, 0, 1)
            : 0.0;
        // 蓄力时轻微抖动
        const QPointF jit(std::sin(m_time * 42.0) * tension * 1.4,
                          std::cos(m_time * 37.0) * tension * 1.4);
        drawBird(p, pos + jit, World::BIRD_R, m_queue.first(),
                 m_pulling ? std::atan2(slingAnchor().y() - pos.y(),
                                        slingAnchor().x() - pos.x()) : 0.0,
                 false, m_time);
    }
}

// ---------------------------------------------------------------- 粒子 -------
void game::paintParticles(QPainter &p) const
{
    for (const Particle &pt : m_particles) {
        const qreal t = clampv(pt.life / qMax(0.01, pt.maxLife), 0.0, 1.0);
        QColor c = pt.color;
        c.setAlpha((int)(c.alpha() * clampv(t * 1.6, 0.0, 1.0)));
        p.save();
        p.translate(pt.pos);
        p.rotate(qRadiansToDegrees(pt.rot));
        p.setPen(Qt::NoPen);
        p.setBrush(c);

        if (pt.type == 1) {                       // 闪光
            p.setBrush(QColor(c.red(), c.green(), c.blue(), c.alpha() / 2));
            p.drawEllipse(QPointF(0, 0), pt.size * (2.0 - t) * 1.7,
                                     pt.size * (2.0 - t) * 1.7);
            p.setBrush(c);
            p.drawEllipse(QPointF(0, 0), pt.size * (2.0 - t), pt.size * (2.0 - t));
        } else if (pt.type == 2) {                // 彩纸
            p.drawRect(QRectF(-pt.size * 0.5, -pt.size * 0.28, pt.size, pt.size * 0.56));
        } else if (pt.type == 3) {                // 羽毛
            p.drawEllipse(QPointF(0, 0), pt.size * 0.9, pt.size * 0.32);
        } else if (pt.type == 5) {                // 烟
            p.setBrush(QColor(c.red(), c.green(), c.blue(), c.alpha() / 3));
            p.drawEllipse(QPointF(0, 0), pt.size, pt.size * 0.82);
        } else {                                  // 碎屑
            p.drawRect(QRectF(-pt.size * 0.5, -pt.size * 0.5, pt.size, pt.size));
        }
        p.restore();
    }
}

// ---------------------------------------------------------------- 瞄准 UI ----
void game::paintAimUi(QPainter &p) const
{
    if (m_state != StAim || m_queue.isEmpty()) return;

    const QPointF a = slingAnchor();
    const QPointF cur = slingBirdPos();

    if (m_pulling) {
        const QPointF pull = a - cur;
        const qreal mag = euclidLen(pull);
        const QPointF dir = unit(pull);

        // ---- 预测轨迹 ----
        // 同样不逐点画圆: 用一条"圆头虚线"描边, 一次就得到整串圆点, 快得多
        const QPointF v0 = pull * World::LAUNCH_K;
        const qreal step = 0.055;
        QPainterPath path;
        bool started = false;
        for (int i = 1; i <= 34; ++i) {
            const qreal t = i * step;
            const QPointF pos = cur + v0 * t + QPointF(0, 0.5 * World::GRAVITY * t * t);
            if (pos.y() > World::GROUND_Y || pos.x() > World::W) break;
            if (!started) { path.moveTo(pos); started = true; }
            else          { path.lineTo(pos); }
        }
        if (started) {
            QPen pen(QColor(255, 255, 255, 215));
            pen.setWidthF(6.0);
            pen.setCapStyle(Qt::RoundCap);
            pen.setDashPattern(QVector<qreal>() << 0.01 << 3.2);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        }

        // ---- 拉伸量指示环 ----
        const qreal power = clampv(mag / World::MAX_PULL, 0.0, 1.0);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, 90), 3.0));
        p.drawEllipse(a, World::MAX_PULL, World::MAX_PULL);
        QColor pc = (power < 0.55) ? QColor(0x7E, 0xD3, 0x5A)
                   : (power < 0.86 ? QColor(0xF5, 0xC5, 0x11) : QColor(0xE0, 0x3A, 0x2B));
        p.setPen(QPen(pc, 5.0, Qt::SolidLine, Qt::RoundCap));
        const qreal span = 300.0 * power;
        p.drawArc(QRectF(a.x() - World::MAX_PULL, a.y() - World::MAX_PULL,
                         World::MAX_PULL * 2, World::MAX_PULL * 2),
                  (90 - span / 2) * 16, span * 16);

        // ---- 速度矢量箭头 ----
        const QPointF tip = cur + dir * (mag * 1.9 + 40.0);
        p.setPen(QPen(QColor(255, 255, 255, 210), 4.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(cur + dir * 24.0, tip);
        QPolygonF head;
        const QPointF n(-dir.y(), dir.x());
        head << tip + dir * 16.0 << tip + n * 9.0 - dir * 4.0 << tip - n * 9.0 - dir * 4.0;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 225));
        p.drawPolygon(head);
    }

    // ---- 静止时：呼吸提示圈 ----
    if (!m_pulling) {
        const qreal pulse = 0.5 + 0.5 * std::sin(m_time * 3.2);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, (int)(70 + 90 * pulse)), 2.5));
        p.drawEllipse(cur, World::BIRD_R + 9.0 + pulse * 5.0,
                           World::BIRD_R + 9.0 + pulse * 5.0);

        // 拖动方向提示箭头
        p.setPen(QPen(QColor(255, 255, 255, 130), 3.5, Qt::SolidLine, Qt::RoundCap));
        const QPointF s0 = cur + QPointF(-30, 26);
        const QPointF s1 = cur + QPointF(-66, 58);
        p.drawLine(s0, s1);
        const QPointF d = unit(s1 - s0);
        const QPointF n(-d.y(), d.x());
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 160));
        p.drawPolygon(QPolygonF() << (s1 + d * 13.0) << (s1 + n * 8.0) << (s1 - n * 8.0));
    }
}

// ---------------------------------------------------------------- HUD --------
void game::paintHud(QPainter &p) const
{
    // HUD 内容只在游戏状态变化时才不同, 而绘制代价不小(好几段文字,
    // 文字渲染在板子上很慢)。用状态做 key 缓存整块 HUD, 命中就直接贴图。
    const QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
            .arg(m_score).arg(m_best).arg(m_level).arg(pigsAliveCount())
            .arg(m_queue.size()).arg(m_musicOn ? 1 : 0)
            .arg(m_hudHintT > 0 ? 1 : 0);

    if (key != m_hudKey || m_hudCache.isNull()
        || m_hudCache.size() != size()) {
        m_hudKey = key;
        m_hudCache = QPixmap(size());
        m_hudCache.fill(Qt::transparent);
        QPainter hp(&m_hudCache);
        hp.setRenderHint(QPainter::Antialiasing, true);
        paintHudBody(hp);
        hp.end();
    }
    p.drawPixmap(0, 0, m_hudCache);
}

void game::paintHudBody(QPainter &p) const
{
    // 左上角计分板
    const QRectF panel(14, 12, 236, 62);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 100));
    p.drawRoundedRect(panel, 12, 12);
    p.setBrush(QColor(255, 255, 255, 30));
    p.drawRoundedRect(panel.adjusted(1.5, 1.5, -1.5, -1.5), 11, 11);

    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(15);
    p.setFont(f);
    p.setPen(QColor(0xFF, 0xE0, 0x6A));
    p.drawText(QRectF(26, 18, 120, 20), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("得分"));

    f.setPixelSize(30);
    p.setFont(f);
    p.setPen(QColor(255, 255, 255));
    p.drawText(QRectF(26, 34, 160, 36), Qt::AlignLeft | Qt::AlignVCenter,
               QString::number(m_score));

    f.setPixelSize(13);
    p.setFont(f);
    p.setPen(QColor(255, 255, 255, 165));
    p.drawText(QRectF(178, 20, 66, 20), Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("最高 %1").arg(m_best));
    p.drawText(QRectF(178, 40, 66, 20), Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("第 %1 关").arg(m_level + 1));

    // ---- 待发射小鸟 ----
    if (!m_queueAll.isEmpty()) {
        const int total = m_queueAll.size();
        const int left  = m_queue.size();
        for (int i = 0; i < total; ++i) {
            const QPointF c(26 + i * 34.0, 100.0);
            const bool spent = (i < total - left);
            p.setOpacity(spent ? 0.28 : 1.0);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 70));
            p.drawEllipse(c + QPointF(0, 2), 15.0, 15.0);
            drawBird(p, c, 13.0, m_queueAll[i], 0.0, false, 0.0);
            p.setOpacity(1.0);
        }
    }

    // ---- 剩余猪 ----
    const int pa = pigsAliveCount();
    if (m_pigsTotal > 0) {
        for (int i = 0; i < m_pigsTotal; ++i) {
            const QPointF c(width() - 34.0, 34.0 + i * 34.0);
            const bool dead = (i >= pa);
            p.setOpacity(dead ? 0.26 : 1.0);
            drawPig(p, c, 12.0, 0.0, 0.0, 1.0);
            p.setOpacity(1.0);
        }
    }

    // ---- 顶部按钮 ----
    struct Btn { QRectF r; QString txt; bool on; };
    QVector<Btn> btns;
    btns << Btn{ m_btnRestart, QStringLiteral("重来"), true }
         << Btn{ m_btnSound,   m_musicOn ? QStringLiteral("音效 开")
                                         : QStringLiteral("音效 关"), m_musicOn }
         << Btn{ m_btnQuit,    QStringLiteral("退出"), true };
    // 结算卡片的按钮走 m_btnOvLeft/m_btnOvRight, 不在这里画

    f.setPixelSize(15);
    p.setFont(f);
    for (const Btn &b : btns) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, b.on ? 105 : 60));
        p.drawRoundedRect(b.r, 10, 10);
        p.setBrush(QColor(255, 255, 255, 26));
        p.drawRoundedRect(b.r.adjusted(1.5, 1.5, -1.5, -1.5), 9, 9);
        p.setPen(b.on ? QColor(255, 255, 255, 235) : QColor(255, 255, 255, 120));
        p.drawText(b.r, Qt::AlignCenter, b.txt);
    }

    // ---- 开局操作提示 ----
    if (m_hudHintT > 0 && m_state == StAim && m_level == 0) {
        const qreal al = clampv(m_hudHintT / 1.2, 0.0, 1.0);
        const QRectF hr(World::W * 0.5 - 250, 556, 500, 34);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, (int)(110 * al)));
        p.drawRoundedRect(hr, 16, 16);
        f.setPixelSize(17);
        p.setFont(f);
        p.setPen(QColor(255, 255, 255, (int)(232 * al)));
        p.drawText(hr, Qt::AlignCenter,
                   QStringLiteral("按住小鸟往后拉，松手发射 —— 打光所有的猪就赢啦"));
    }
}

// ---------------------------------------------------------------- 结算 -------
void game::paintOverlay(QPainter &p) const
{
    if (m_state != StWin && m_state != StLose) return;

    const qreal t = clampv(m_winT / 0.45, 0.0, 1.0);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, (int)(130 * t)));
    p.fillRect(rect(), QColor(0, 0, 0, (int)(130 * t)));

    const QRectF card(width() * 0.5 - 250, height() * 0.5 - 150, 500, 300);
    p.save();
    p.translate(card.center());
    p.scale(0.86 + 0.14 * t, 0.86 + 0.14 * t);
    p.translate(-card.center());
    p.setOpacity(t);

    const bool win = (m_state == StWin);

    // 卡片
    QLinearGradient cg(card.topLeft(), card.bottomLeft());
    if (win) {
        cg.setColorAt(0, QColor(0x4C, 0xA8, 0x3A));
        cg.setColorAt(1, QColor(0x2E, 0x74, 0x22));
    } else {
        cg.setColorAt(0, QColor(0x5A, 0x5F, 0x6B));
        cg.setColorAt(1, QColor(0x33, 0x36, 0x3E));
    }
    p.setBrush(cg);
    p.drawRoundedRect(card, 22, 22);
    p.setPen(QPen(QColor(255, 255, 255, 70), 3.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(card.adjusted(4, 4, -4, -4), 18, 18);

    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(46);
    p.setFont(f);
    p.setPen(QColor(255, 255, 255));
    p.drawText(QRectF(card.left(), card.top() + 20, card.width(), 56),
               Qt::AlignCenter,
               win ? QStringLiteral("过 关 啦 !") : QStringLiteral("失 败 了"));

    // 星级
    if (win) {
        for (int i = 0; i < 3; ++i) {
            const QPointF c(card.center().x() + (i - 1) * 96.0, card.top() + 122);
            const bool on = (i < m_stars);
            p.save();
            p.translate(c);
            p.rotate(-14.0);
            QPainterPath star;
            for (int k = 0; k < 10; ++k) {
                const qreal ang = -kPI / 2.0 + k * kPI / 5.0;
                const qreal rr  = (k % 2 == 0) ? 40.0 : 17.0;
                const QPointF pt(std::cos(ang) * rr, std::sin(ang) * rr);
                if (k == 0) star.moveTo(pt); else star.lineTo(pt);
            }
            star.closeSubpath();
            if (on) {
                QLinearGradient sg(0, -40, 0, 40);
                sg.setColorAt(0, QColor(0xFF, 0xF0, 0x8A));
                sg.setColorAt(1, QColor(0xF2, 0xB1, 0x0C));
                p.setBrush(sg);
                p.setPen(QPen(QColor(0xC0, 0x82, 0x00), 2.5));
            } else {
                p.setBrush(QColor(255, 255, 255, 42));
                p.setPen(QPen(QColor(255, 255, 255, 70), 2.5));
            }
            p.drawPath(star);
            p.restore();
        }
    }

    f.setPixelSize(20);
    p.setFont(f);
    p.setPen(QColor(255, 255, 255, 225));
    p.drawText(QRectF(card.left(), card.top() + 180, card.width(), 26),
               Qt::AlignCenter,
               QStringLiteral("本关得分 %1    ·    最高分 %2").arg(m_score).arg(m_best));

    // 按钮
    const QRectF b1(card.center().x() - 200, card.bottom() - 72, 180, 50);
    const QRectF b2(card.center().x() +  20, card.bottom() - 72, 180, 50);
    const bool hasNext = (m_level < 2);

    struct PB { QRectF r; QString txt; QColor c1, c2; };
    QVector<PB> pbs;
    if (win) {
        pbs << PB{ b1, QStringLiteral("重玩本关"), QColor(0xE8, 0xA9, 0x1C), QColor(0xC0, 0x82, 0x0C) }
            << PB{ b2, hasNext ? QStringLiteral("下一关") : QStringLiteral("再来一轮"),
                   QColor(0x3E, 0x9B, 0xE0), QColor(0x22, 0x6E, 0xA8) };
    } else {
        pbs << PB{ b1, QStringLiteral("返回主界面"), QColor(0x6B, 0x72, 0x80), QColor(0x45, 0x4A, 0x55) }
            << PB{ b2, QStringLiteral("再试一次"),   QColor(0xE8, 0xA9, 0x1C), QColor(0xC0, 0x82, 0x0C) };
    }
    // 记录按钮热区，供 mousePressEvent 命中检测（paintOverlay 是 const，故 mutable）。
    // 注意用 m_btnOvLeft/m_btnOvRight 这组独立矩形, 千万不能写进 m_btnRestart ——
    // 那是顶部 HUD「重来」的位置, 被覆盖后按钮会画到卡片这里来。
    m_btnOvLeft  = b1;              // 左：重玩本关 / 返回主界面
    m_btnOvRight = b2;              // 右：下一关   / 再试一次

    f.setPixelSize(21);
    p.setFont(f);
    for (const PB &b : pbs) {
        QLinearGradient bg(b.r.topLeft(), b.r.bottomLeft());
        bg.setColorAt(0, b.c1);
        bg.setColorAt(1, b.c2);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 70));
        p.drawRoundedRect(b.r.adjusted(0, 4, 0, 0), 14, 14);
        p.setBrush(bg);
        p.drawRoundedRect(b.r, 14, 14);
        p.setPen(QPen(QColor(255, 255, 255, 120), 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(b.r.adjusted(2, 2, -2, -2), 12, 12);
        p.setPen(QColor(255, 255, 255));
        p.drawText(b.r, Qt::AlignCenter, b.txt);
    }

    p.setOpacity(1.0);
    p.restore();
}

// ============================================================================
//  第七部分：输入
// ============================================================================
void game::mousePressEvent(QMouseEvent *e)
{
    const QPointF sp = e->pos();
    const QPointF wp = screenToWorld(sp);

    // ---- 结算界面优先 ----
    // 必须先判结算: 过关/失败时卡片是模态地盖在最上面的, 而且卡片按钮的矩形
    // 有可能和顶部按钮重叠, 先判顶部的话会把点卡片的操作错当成点顶部按钮。
    if (m_state == StWin || m_state == StLose) {
        const bool win = (m_state == StWin);
        if (win) {
            if (m_btnOvLeft.contains(sp))  { startLevel(m_level); return; }
            if (m_btnOvRight.contains(sp)) {
                if (m_level < 2) startLevel(m_level + 1);
                else             startLevel(0);
                return;
            }
        } else {
            if (m_btnOvLeft.contains(sp))  { close(); return; }
            if (m_btnOvRight.contains(sp)) { startLevel(m_level); return; }
        }
        return;                     // 结算界面上其它地方一律不响应
    }

    // ---- 顶部按钮 ----
    if (m_btnRestart.contains(sp)) {
        startLevel(m_level);
        return;
    }
    if (m_btnSound.contains(sp)) {
        m_musicOn = !m_musicOn;
        return;
    }
    if (m_btnQuit.contains(sp)) {
        close();
        return;
    }

    // ---- 拖拽小鸟 ----
    if (m_state == StAim && !m_queue.isEmpty()) {
        const QPointF anchor = slingAnchor();
        const QPointF bird   = slingBirdPos();
        if (euclidLen(wp - bird) < 90.0
            || euclidLen(wp - anchor) < World::MAX_PULL + 30.0) {
            m_pulling = true;
            m_pullPos = wp;
            if (euclidLen(wp - anchor) > World::MAX_PULL)
                m_pullPos = anchor + unit(wp - anchor) * World::MAX_PULL;
        }
    }
}

void game::mouseMoveEvent(QMouseEvent *e)
{
    m_hovering = true;
    if (!m_pulling) return;
    const QPointF wp = screenToWorld(e->pos());
    const QPointF anchor = slingAnchor();
    QPointF d = wp - anchor;
    const qreal mag = euclidLen(d);
    if (mag > World::MAX_PULL) d = d * (World::MAX_PULL / mag);
    m_pullPos = anchor + d;
}

void game::mouseReleaseEvent(QMouseEvent *)
{
    if (!m_pulling) return;
    m_pulling = false;

    const QPointF pull = slingAnchor() - m_pullPos;
    const qreal mag = euclidLen(pull);
    if (mag < 22.0) {                     // 拉得太少，当作取消
        m_pullPos = slingAnchor();
        return;
    }
    launchBird(pull * World::LAUNCH_K);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void game::enterEvent(QEnterEvent *) { m_hovering = true; }
#else
void game::enterEvent(QEvent *) { m_hovering = true; }
#endif

void game::leaveEvent(QEvent *) { m_hovering = false; }

void game::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_Escape:
        close();
        break;
    case Qt::Key_R:
        if (m_state != StWin && m_state != StLose) startLevel(m_level);
        break;
    case Qt::Key_N:
        startLevel((m_level < 2) ? m_level + 1 : 0);
        break;
    case Qt::Key_Space:
        if (m_state == StWin || m_state == StLose) {
            if (m_state == StWin) startLevel((m_level < 2) ? m_level + 1 : 0);
            else                  startLevel(m_level);
        }
        break;
    default:
        QDialog::keyPressEvent(e);
    }
}
