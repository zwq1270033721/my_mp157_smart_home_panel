#ifndef GAME_H
#define GAME_H

// ============================================================================
//  愤怒的小鸟 (Angry Birds) —— QPainter 纯手工绘制 + 自研 2D 刚体物理
//
//  设计要点（针对 STM32MP157 / linuxfb 无 GPU 环境）:
//   * 不加载任何图片资源，全部图形用 QPainter 路径绘制，二进制体积小、无解码开销
//   * 自研冲量法物理: 圆形(鸟/猪) + 旋转矩形(积木)，支持摩擦/恢复系数/休眠
//   * 音效用 QAudioOutput 实时合成 PCM，零资源文件
//   * 同时兼容 Qt 5.12 (板子) 与 Qt 6.4 (桌面)，差异处用 QT_VERSION 宏隔离
// ============================================================================

#include <QDialog>
#include <QVector>
#include <QPointF>
#include <QSizeF>
#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QHash>
#include <QTimer>
#include <QtGlobal>

#include <functional>

// 音频输出流在两个大版本里叫不同名字，而且 Qt6 的 QAudioOutput 是
// QMediaPlayer 那一挂的另一个类，不能混用，所以这里做个别名统一：
//   Qt5.12 → QAudioOutput   (QAudioDeviceInfo 构造)
//   Qt6.x  → QAudioSink     (QAudioDevice 构造)
QT_BEGIN_NAMESPACE
class QIODevice;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
class QAudioSink;
#else
class QAudioOutput;
#endif
QT_END_NAMESPACE

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
typedef QAudioSink   GameAudioSink;
#else
typedef QAudioOutput GameAudioSink;
#endif

class GameAudio;         // 实时 PCM 合成器 (定义于 game.cpp)

namespace Ui {
class game;
}

// ---------------------------------------------------------------- 世界常量 --
namespace World {
const qreal GRAVITY    = 1500.0;   // 重力加速度 px/s^2
const qreal GROUND_Y   = 520.0;    // 地面线（逻辑坐标）
const qreal W          = 1024.0;   // 逻辑画布宽
const qreal H          = 600.0;    // 逻辑画布高

const qreal MAX_PULL   = 105.0;    // 弹弓最大拉伸半径
// 拉伸距离 -> 初速度 系数。满拉 38° 仰角射程约 960px, 正好覆盖木屋(x=700~1000)
// 木屋在 x=700~1000(弹弓 x=150), 用七八成力度即可命中。
const qreal LAUNCH_K   = 11.0;
const qreal BIRD_DRAG  = 0.04;     // 飞行空气阻力(每秒衰减比例)
const qreal BIRD_R     = 17.0;     // 鸟半径
const qreal PIG_R      = 20.0;     // 猪半径
// 全场静止多久判定回合结束。原来 1.30s, 但判定是"任一物体还在动就重新计时",
// 慢速滚动/轻微摇晃会把计时反复清零, 手感上就是"等半天轮不到下一只鸟"。
// 0.65s 既明显缩短等待, 又留足时间让塌方/碎块落定, 不会在还在塌时就切下一只。
const qreal STOP_TIME  = 0.65;
// 判定"已停下"的速度阈值。原来偏紧(26/34/42), 落地后残余的微小滑动会一直
// 卡在阈值上方, 让计时器永远归零。放宽一点, 肉眼看不出的微动就不算动。
const qreal STILL_VEL  = 55.0;     // 积木/猪/鸟 通用的静止速度阈值
const qreal STILL_ANG  = 0.85;     // 角速度静止阈值
// 发射后的硬上限: 不管塌完没有, 最多等这么久就把操作权交回玩家。
// 塌方可以自己继续塌, 不该把玩家晾着干等 —— 结构慢慢滚的时候,
// "全场静止"是有可能一直等不到的。
const qreal MAX_TURN_WAIT = 3.0;
const int   MAX_BIRDS  = 4;        // 单关最多鸟数
}

// ------------------------------------------------------------------ 实体 ----
struct Bird {
    QPointF pos, vel;
    qreal   r      = World::BIRD_R;
    QColor  color;
    bool    flying = false;
    bool    rest   = false;      // 已落地停止
    bool    alive  = true;
    qreal   rot    = 0;
    qreal   flapT  = 0;          // 扑翼动画计时
    qreal   lifeT  = 0;
    QVector<QPointF> trail;      // 飞行轨迹残留
};

struct Pig {
    QPointF pos, vel;
    qreal   r    = World::PIG_R;
    int     hp   = 100;
    bool    alive = true;
    qreal   hurtT = 0;           // 受击闪白计时
    qreal   blinkPhase = 0;
    qreal   squash = 1.0;        // 挤压形变
    int     idx  = 0;
};

struct Block {
    QPointF  pos;                // 中心
    QSizeF   size;
    qreal    angle = 0;          // 弧度
    QPointF  vel;
    qreal    angVel = 0;
    qreal    mass = 1.0;
    qreal    invMass = 1.0;
    qreal    invI  = 1.0;
    int      mat   = 0;          // 0=木 1=石 2=玻璃
    int      hp    = 100;
    bool     alive = true;
    qreal    hurtT = 0;
    bool     sleeping = true;
    int      kind  = 0;          // 0=普通积木 1=深色木箱 2=TNT 炸药箱
    qreal    seed  = 0;          // 纹理随机种子（保持每块花纹固定）
    int      settleFrames = 0;   // 连续够慢的帧数, 够了才允许休眠
};

struct Particle {
    QPointF pos, vel;
    qreal   life, maxLife, size;
    QColor  color;
    int     type;                // 0=碎屑 1=闪光 2=彩纸 3=羽毛 4=星 5=烟
    qreal   rot, spin;
};

struct Plank {                   // 积木被打碎后飞出的碎块
    QPointF pos, vel;
    QSizeF  size;
    qreal   angle, angVel, life;
    int     mat;
};

// -------------------------------------------------------------- game 类 ----
class game : public QDialog
{
    Q_OBJECT

public:
    explicit game(QWidget *parent = nullptr);
    ~game();

protected:
    void paintEvent(QPaintEvent *e) override;   // 需要 e->rect() 判断暴露范围
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent *e) override;
#else
    void enterEvent(QEvent *e) override;
#endif
    void leaveEvent(QEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private slots:
    void onTick();

private:
    Ui::game *ui;

    // ------------------------------------------------------------ 状态机 --
    enum State { StAim, StFly, StSettle, StWin, StLose };
    State m_state = StAim;

    // ------------------------------------------------------------ 世界 ----
    QVector<QColor>    m_queue;      // 待发射的鸟（front() 正在弹弓上）
    QVector<QColor>    m_queueAll;   // 本关初始鸟序列（用于 HUD 统计）
    QVector<Bird>      m_birds;      // 已发射、正在世界里的鸟
    QVector<Pig>       m_pigs;
    QVector<Block>     m_blocks;     // 结构积木
    QVector<Particle>  m_particles;
    QVector<Plank>     m_debris;

    int    m_level    = 1;
    int    m_score    = 0;
    int    m_best     = 0;
    int    m_stars    = 0;
    int    m_pigsTotal = 0;
    qreal  m_settleT  = 0;
    // 开场保护期: 进关卡后的头 1.5 秒不结算碰撞伤害。
    // 关卡是静态搭出来的, 但摆位难免有零点几像素的误差, 求解器会把它当成
    // 持续挤压, 每帧产生接近速度 → 扣血。TNT 只有 20 血, 一两秒就被磨爆
    // ——表现就是"还没开始玩, 炸弹自己炸了"。有了保护期, 这点微小误差
    // 会在重力作用下自己贴合, 不会伤到结构。
    qreal  m_graceT   = 0;
    qreal  m_lastShotT = 0;   // 上一发时刻, 用于 MAX_TURN_WAIT 超时放行
    qreal  m_winT     = 0;
    qreal  m_time     = 0;            // 全局时间（秒）
    qreal  m_hudHintT = 4.0;          // 开局操作提示显示时长

    // ------------------------------------------------------------ 输入 ----
    bool     m_pulling   = false;
    QPointF  m_pullPos;               // 鸟被拖到的世界坐标
    bool     m_hovering  = false;     // 指针是否在窗口内
    // 顶部 HUD 按钮热区(重来/音效/退出)。只由 layoutHudButtons() 写入。
    mutable QRectF m_btnRestart, m_btnSound, m_btnQuit;
    // 结算卡片上的两个按钮热区(左=重玩本关/返回主界面, 右=下一关/再试一次)。
    // 必须和上面那组分开! 之前两处共用 m_btnRestart/m_btnNext, paintOverlay()
    // 会把顶部"重来"的矩形改写成卡片里的坐标, 于是过关后进下一关时,
    // 顶部"重来"按钮会被画到屏幕正中间(而且点击热区也跟着跑偏)。
    // paintOverlay() 是 const, 故为 mutable。
    mutable QRectF m_btnOvLeft, m_btnOvRight;
    bool     m_musicOn = true;

    // ------------------------------------------------------------ 音效 ----
    GameAudio     *m_audio    = nullptr;
    GameAudioSink *m_audioOut = nullptr;
    QIODevice     *m_audioDev = nullptr;
    QTimer        *m_timer    = nullptr;   // 驱动 onTick 的帧定时器(析构要先停)
    void initAudio();
    void playSfx(int type);

    // ------------------------------------------------------------ 核心 ----
    void startLevel(int idx);
    void launchBird(const QPointF &vel);
    void stepPhysics(qreal dt);
    void resolveCollisions();
    void killPig(int i, qreal power);
    void damageBlock(Block &b, qreal power);
    void explode(const QPointF &p, qreal radius, qreal power);
    void handleRoundEnd(qreal dt);

    bool  worldSettled() const;
    int   pigsAliveCount() const;
    QPointF slingAnchor() const;
    QPointF slingBirdPos() const;

    // ---- 静态背景缓存 ----
    // 天空/远山/近山/树/草地/太阳/云 每帧都一样, 原生分辨率下重画一次要 38ms,
    // 根本达不到流畅帧率。改成开局预渲染成一张图, 之后每帧只 drawImage 一次。
    // 板子 linuxfb 是 16bpp, 用 RGB32 会被迫每帧转换, 所以按屏深对齐。
    void buildStaticCache();
    QImage m_bgCache;

    // ---- 精灵缓存 ----
    // 积木/猪/鸟每帧重画一遍渐变+纹理代价太高(实测实体层 24ms),
    // 但它们"长得怎么样"只取决于 材质/大小/颜色, 与位置姿态无关,
    // 所以按外形渲染一次存成 QPixmap, 之后只做旋转+贴图。
    QPixmap sprite(const QString &key, const QSize &sz,
                   const std::function<void(QPainter &)> &render) const;
    void renderBlockSprite(QPainter &q, const Block &b) const;
    void renderPigBody(QPainter &q, qreal r) const;
    void renderBirdSprite(QPainter &q, qreal r, const QColor &body) const;
    mutable QHash<QString, QPixmap> m_sprites;

    // HUD(计分板/小鸟队列/剩余猪/按钮)每帧重画太贵, 用状态做 key 缓存
    mutable QPixmap m_hudCache;
    mutable QString m_hudKey;

    // ------------------------------------------------------------ 绘制 ----
    void paintSky(QPainter &p) const;
    void paintHills(QPainter &p) const;
    void paintGround(QPainter &p) const;
    void paintSlingBack(QPainter &p) const;
    void paintSlingFront(QPainter &p) const;
    void paintBlocks(QPainter &p) const;
    void paintDebris(QPainter &p) const;
    void paintPigs(QPainter &p) const;
    void paintBirds(QPainter &p) const;
    void paintParticles(QPainter &p) const;

    void drawBird(QPainter &p, const QPointF &c, qreal r, const QColor &body,
                  qreal rot, bool flying, qreal flap) const;
    void drawPig(QPainter &p, const QPointF &c, qreal r, qreal hurt,
                 qreal blink, qreal squash) const;
    void drawBlockShape(QPainter &p, const Block &b) const;
    void drawPlank(QPainter &p, const Plank &pl) const;
    void drawBand(QPainter &p, const QPointF &to, bool front) const;
    void drawThickLine(QPainter &p, const QPointF &a, const QPointF &b,
                       const QColor &c, qreal w) const;

    // ------------------------------------------------------------ HUD ----
    void paintHud(QPainter &p) const;        // 带缓存的入口
    void paintHudBody(QPainter &p) const;    // 真正的绘制
    void layoutHudButtons();                 // 重算顶部三个按钮的矩形

    void paintAimUi(QPainter &p) const;
    void paintOverlay(QPainter &p) const;

    void emitParticles(const QPointF &p, int n, int type, qreal spread,
                       qreal speed, const QColor &c, qreal size);
    void addScore(int v);

    void buildLevel1();
    void buildLevel2();
    void buildLevel3();

    // ------------------------------------------------------------ 工具 ----
    static QPointF rot2(const QPointF &v, qreal a);
    static qreal   cross(const QPointF &a, const QPointF &b);
    static QPointF crossS(qreal w, const QPointF &r);
    static qreal   clampd(qreal v, qreal lo, qreal hi);
    static QColor  lighten(const QColor &c, int d);
    static QColor  darken(const QColor &c, int d);

    void    blockCorners(const Block &b, QPointF out[4]) const;
    qreal   scaleF() const;
    QPointF viewOffset() const;
    QPointF screenToWorld(const QPointF &s) const;

    // ---- 场景静态层 + 脏区重绘 ----
    // 猪、积木、弹弓这些在两次撞击之间是不动的, 一起烘进 sceneCache。
    // 每帧只把"有东西在动"的那一小块从缓存拷回来再重画, 其余像素一个不碰。
    // 一块积木"这一帧不动"吗? 只看 sleeping 是不够的:
    // 被猪压着的横梁会被持续接触冲量一直唤醒, 但它位置其实没变。
    static bool blockIsStill(const Block &b)
    {
        return b.sleeping
            || (b.vel.manhattanLength() < 3.0 && qAbs(b.angVel) < 0.08);
    }

    void   bakeSceneLayer(QPainter &q) const;
    void   rebuildSceneLayer();       // 关卡切换/尺寸变化时重建
    void   restoreDirty(QPainter &p, const QRect &wantPx);
    // 收集本帧"活动物体"(发射区+撞击区)的世界坐标包围盒
    void   collectActiveBox();
    QImage m_sceneCache;
    QRect  m_actBox;                  // 逻辑坐标下的活动范围
    bool   m_haveActive = false;
    bool   m_dirtyAll  = true;        // 首次/重建后需要整屏恢复
    // 记录"上一帧有哪些积木被烘进了场景层"(= 睡着的),
    // 有变化才重烘, 避免塌方时每帧重烘整层
    QVector<bool> m_bakedStill;
    // 场景层里"弹弓上那只鸟"的指纹(颜色+剩余数+是否瞄准中), 变了才重烘
    uint m_bakedBirdKey = 0;
};

#endif // GAME_H
