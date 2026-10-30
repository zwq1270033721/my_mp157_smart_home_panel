#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QTimer>
#include <QPropertyAnimation>
#include <QEasingCurve>

#include "musicdialog.h"
#include "lightdialog.h"
#include "cameradialog.h"
#include "bluetoothdialog.h"
#include "wifidialog.h"
#include "lockdialog.h"
#include "sundialog.h"
#include "waterdialog.h"
#include "weatherdialog.h"
#include "moondialog.h"
#include "shielddialog.h"
#include "tvdialog.h"
#include "qqdialog.h"
#include "homedialog.h"
#include "serialport.h"
#include "keyboardialog.h"
#include "game.h"

#include <QApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QStackedWidget>
#include <QTouchEvent>
#include <QWidget>
#include <QtMath>

void slideInBottomFrame();
// 桌面 Qt6 与板子 Qt5.12.9 的鼠标/触摸 API 不同，这里做一层兼容封装：
//   Qt6: QMouseEvent::globalPosition()  QTouchEvent::points()
//   Qt5: QMouseEvent::globalPos()       QTouchEvent::touchPoints()
static QPoint globalPosOf(const QMouseEvent *me)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return me->globalPosition().toPoint();
#else
    return me->globalPos();
#endif
}

static bool touchPointsEmpty(const QTouchEvent *te)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return te->points().isEmpty();
#else
    return te->touchPoints().isEmpty();
#endif
}

static QPoint touchGlobalPos(const QTouchEvent *te)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return te->points().first().globalPosition().toPoint();
#else
    return te->touchPoints().first().screenPos().toPoint();
#endif
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , propertyAnimation(nullptr)   // 必须初始化：否则是野指针，首次滑动时 stop() 会段错误
{
    ui->setupUi(this);

    //QTimer::singleShot(100,this,&MainWindow::slideInBottomFrame);
    // 让背景标签不拦截鼠标/触摸事件，事件会透传到下方的 page
    ui->label->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    // 全局安装事件过滤器：捕获 stackedWidget 内所有子控件的鼠标/触摸事件
    // 这样无论点在页面的哪个子控件上，都能识别滑动手势
    qApp->installEventFilter(this);

    m_pressed = false;
}

MainWindow::~MainWindow()
{
    delete ui;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // 只处理 stackedWidget 及其子控件上的事件
    QWidget *w = qobject_cast<QWidget *>(watched);
    if (!w || !ui->stackedWidget->isAncestorOf(w)) {
        return QMainWindow::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        QMouseEvent *me = static_cast<QMouseEvent *>(event);
        // 转换为 stackedWidget 的相对坐标，保证不同子控件上的坐标一致
        m_pressPos = ui->stackedWidget->mapFromGlobal(globalPosOf(me));
        m_pressed  = true;
        break;
    }
    case QEvent::MouseButtonRelease: {
        if (!m_pressed) break;
        m_pressed = false;

        QMouseEvent *me = static_cast<QMouseEvent *>(event);
        QPoint releasePos = ui->stackedWidget->mapFromGlobal(globalPosOf(me));
        QPoint delta = releasePos - m_pressPos;

        // 横向滑动判定：水平位移 > 垂直位移 且 > 50px
        if (qAbs(delta.x()) > qAbs(delta.y()) && qAbs(delta.x()) > 50) {
            int cur = ui->stackedWidget->currentIndex();
            int cnt = ui->stackedWidget->count();

            // 左划 -> 下一页, 右划 -> 上一页
            int next = (delta.x() < 0) ? cur + 1 : cur - 1;
            if (next >= 0 && next < cnt) {
                ui->stackedWidget->setCurrentIndex(next);

                // 翻页后才判断: 只有新页确有面板时才播放（无面板的页自动跳过）
                slideInBottomFrame();
            }
        }
        break;
    }
    case QEvent::TouchBegin: {
        QTouchEvent *te = static_cast<QTouchEvent *>(event);
        if (!touchPointsEmpty(te)) {
            m_pressPos = ui->stackedWidget->mapFromGlobal(touchGlobalPos(te));
            m_pressed  = true;
        }
        break;
    }
    case QEvent::TouchEnd: {
        if (!m_pressed) break;
        m_pressed = false;

        QTouchEvent *te = static_cast<QTouchEvent *>(event);
        if (!touchPointsEmpty(te)) {
            QPoint releasePos = ui->stackedWidget->mapFromGlobal(touchGlobalPos(te));
            QPoint delta = releasePos - m_pressPos;

            if (qAbs(delta.x()) > qAbs(delta.y()) && qAbs(delta.x()) > 50) {
                int cur = ui->stackedWidget->currentIndex();
                int cnt = ui->stackedWidget->count();

                // 与鼠标分支保持一致的翻页 + 面板动画逻辑
                int next = (delta.x() < 0) ? cur + 1 : cur - 1;
                if (next >= 0 && next < cnt) {
                    ui->stackedWidget->setCurrentIndex(next);
                    slideInBottomFrame();
                }
            }
        }
        break;
    }
    default:
        break;
    }

    return QMainWindow::eventFilter(watched, event);
}

// ===== 11 个设备按钮点击槽函数：打开对应的设备控制 Dialog =====

void MainWindow::on_btnMusic_clicked()
{
    MusicDialog dlg(this);
    dlg.exec();
}

void MainWindow::on_btnLight_clicked()
{
    LightDialog dlg(this);
    dlg.exec();
}

void MainWindow::on_btnCamera_clicked()
{
    CameraDialog dlg(this);
    dlg.exec();
}

void MainWindow::on_btnBluetooth_clicked()
{
    BluetoothDialog dlg(this);
    dlg.exec();
}

void MainWindow::on_btnWifi_clicked()
{
    WifiDialog dlg(this);
    dlg.exec();
}

void MainWindow::on_btnLock_clicked()
{
    LockDialog dlg(this);
    dlg.exec();
}

void MainWindow::on_btnSun_clicked()
{
    SunDialog dlg(this);
    dlg.exec();
}

void MainWindow::on_btnWater_clicked()
{
    WaterDialog dlg(this);
    dlg.exec();
}

void MainWindow::on_btnWeather_clicked()
{
    WeatherDialog dlg(this);
    dlg.exec();
}

void MainWindow::on_btnMoon_clicked()
{
    MoonDialog dlg(this);
    dlg.exec();
}

void MainWindow::on_btnShield_clicked()
{
    ShieldDialog dlg(this);
    dlg.exec();
}

// ===== page_4 上的按钮（功能与 page_3 相同） =====

void MainWindow::on_btnCamera2_clicked()
{
    CameraDialog dlg(this);
    dlg.exec();
}

void MainWindow::on_btnMoon2_clicked()
{
    MoonDialog dlg(this);
    dlg.exec();
}

void MainWindow::on_btnShield2_clicked()
{
    ShieldDialog dlg(this);
    dlg.exec();
}

void MainWindow::on_btnWeather2_clicked()
{
    WeatherDialog dlg(this);
    dlg.exec();
}

// ===== page_2 上的按钮（功能相同） =====

void MainWindow::on_btnCamera_2_clicked()
{
    CameraDialog dlg(this);
    dlg.exec();
}

void MainWindow::on_btnMoon_2_clicked()
{
    MoonDialog dlg(this);
    dlg.exec();
}

void MainWindow::on_btnShield_2_clicked()
{
    ShieldDialog dlg(this);
    dlg.exec();
}

void MainWindow::on_btnWeather_2_clicked()
{
    WeatherDialog dlg(this);
    dlg.exec();
}

// ===== page_3 上的 APP 按钮 =====

void MainWindow::on_btnTv_clicked()
{
    TvDialog dlg(this);
    dlg.exec();
}

void MainWindow::on_btnQq_clicked()
{
    QqDialog dlg(this);
    dlg.exec();
}

void MainWindow::on_btnHome_clicked()
{
    HomeDialog dlg(this);
    dlg.exec();
}

void MainWindow::on_galagame_clicked()
{
    // 和其他 11 个设备页保持一致：栈上对象 + 模态 exec()，返回时自动析构。
    // (game 内部已置 WA_QuitOnClose=false，关窗口不会连带退出整个程序)
    game dlg(this);
    dlg.exec();

    // 板子跑 linuxfb，没有窗口管理器/合成器：全屏游戏窗口消失后，
    // 被它整个盖住的主窗口不会自动收到重绘，画面会定格在游戏最后一帧，
    // 看起来同样像"整个程序都关了"。返回后主动重绘一次，把主界面刷回屏幕。
    show();
    raise();
    update();
    repaint();          // linuxfb 上必须同步重绘，才会立刻写回 /dev/fb0
}



void MainWindow::on_pushButton_clicked()
{
    SerialPort dlg(this);
    dlg.exec();
}


void MainWindow::on_keyboardialog_clicked()
{
    KeyBoarDialog *dlg=new KeyBoarDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

// 三个带底部面板的页面，面板 objectName 分别是
// appwindows / appwindows_2 / appwindows_3（page 那页没有面板）。
// uic 为每个名字生成独立指针，写死其中任何一个都只能动一页，
// 所以这里按前缀在"当前页"里查找，页序调整后也不会失效。
QWidget *MainWindow::currentSlidePanel() const
{
    QWidget *page = ui->stackedWidget->currentWidget();
    if (!page)
        return nullptr;

    const auto frames = page->findChildren<QFrame *>(QString(), Qt::FindDirectChildrenOnly);
    for (QFrame *f : frames) {
        if (f->objectName().startsWith(QLatin1String("appwindows")))
            return f;
    }
    return nullptr;
}

void MainWindow::slideInBottomFrame()
{
    QWidget *panel = currentSlidePanel();
    if (!panel)
        return;   // 该页没有面板（如 page），直接不播放

    QRect endRect = panel->geometry();
    QRect startRect = endRect;
    startRect.translate(0,100);

    if(propertyAnimation)
    {
        propertyAnimation->stop();
        delete propertyAnimation;
        propertyAnimation=nullptr;
    }
    // 第三个参数把动画挂到 this 下：窗口关闭时动画随之销毁，
    // 否则动画还在跑而 panel 已析构，关闭时会崩
    propertyAnimation = new QPropertyAnimation(panel, "geometry", this);
    propertyAnimation->setStartValue(startRect);
    propertyAnimation->setEndValue(endRect);
    propertyAnimation->setDuration(500);
    propertyAnimation->setEasingCurve(QEasingCurve::OutBack);
    propertyAnimation->start();


}


