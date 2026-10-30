#include "musicdialog.h"
#include "ui_MusicDialog.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLabel>
#include <QRandomGenerator>
#include <QSlider>
#include <QStandardPaths>
#include <QUrl>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QAudioOutput>      // Qt6 的音频输出对象（音量、输出设备）
#include <QMediaMetaData>    // Qt6 的音乐标签（标题/歌手）
#else
#include <QMediaContent>     // Qt5 的 setMedia() 参数类型
#endif

// ============================================================
// MusicDialog 实现
//
// 一、整体数据流
//
//    扫描目录 scanMusic()
//        m_files = ["/home/root/Music/于是.mp3", ...]
//        m_index = -1
//             |
//             v
//    用户点播放 on_btnPlay_clicked() -> togglePlay()
//        第一次：playAt(0) -> QMediaPlayer::setMedia()/setSource() + play()
//        之后  ：play() 继续 / pause() 暂停
//             |
//             +--> positionChanged      -> 进度条 + 当前时间
//             +--> durationChanged      -> 进度条最大值 + 总时长
//             +--> mediaStatusChanged   -> EndOfMedia 时按播放模式切歌
//
//    用户拖动进度条 -> sliderReleased -> setPosition()
//    用户拖动音量条 -> valueChanged   -> applyVolume()
//
// 二、Qt5 / Qt6 的差异（本文件里用 #if QT_VERSION 区分）
//
//    功能           Qt5.12（板子）                 Qt6（桌面）
//    ------------------------------------------------------------
//    设置媒体       setMedia(QMediaContent(url))  setSource(url)
//    播放状态       state()                       playbackState()
//    音量           QMediaPlayer::setVolume(0~100) QAudioOutput::setVolume(0.0~1.0)
//    媒体标签       metaData("Title")             QMediaMetaData::Title
//    播放列表       有 QMediaPlaylist             已删除，自己用 QStringList
//
// 三、中文文件名
//
//    板子的 locale 是 POSIX/C，Qt5 会把中文路径解码成乱码，
//    导致 QMediaPlayer 报 "Resource not found"。
//    修复在 main.cpp 里（setlocale + QTextCodec::setCodecForLocale）。
// ============================================================


MusicDialog::MusicDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::MusicDialog)
{
    ui->setupUi(this);

    // ---------------------------------------------------------
    // 1. 创建播放器
    //
    //    QMediaPlayer 只负责“放一个文件”：
    //      - 解码：板子走 GStreamer，桌面走 FFmpeg/GStreamer
    //      - 输出：最终交给 PulseAudio（板子）/ PipeWire（桌面）
    //
    //    列表、上一首、下一首、播放模式都由本类自己维护，
    //    因为 Qt6 已经删掉了 QMediaPlaylist。
    // ---------------------------------------------------------
    mediaPlayer = new QMediaPlayer(this);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Qt6 必须显式接一个 QAudioOutput：
    //   - 不接 -> 进度条会动，但一点声音都没有
    //   - 音量也在这个对象上，不在 QMediaPlayer 上
    // Qt5 不需要这一步，QMediaPlayer 自己管音频输出。
    audioOutput = new QAudioOutput(this);
    mediaPlayer->setAudioOutput(audioOutput);
#endif

    // ---------------------------------------------------------
    // 2. 扫描音乐目录
    //
    //    板子固定 /home/root/Music；
    //    桌面没有这个目录时退回系统音乐目录（一般是 ~/Music）。
    // ---------------------------------------------------------
    scanMusic();

    // ---------------------------------------------------------
    // 3. 音量滑条默认值是 0（等于静音），先设成 50%
    //
    //    setValue(135) 会发出 valueChanged 信号，
    //    进而触发自动连接的 on_sliderProgresssound_valueChanged()，
    //    真正去调用 applyVolume()。所以这里只改滑条值就够了。
    // ---------------------------------------------------------
    ui->sliderProgresssound->setValue(135);      // 135 / 270 = 50%

    // ---------------------------------------------------------
    // 4. 播放位置变化 -> 进度条 + 当前时间
    //
    //    positionChanged 播放过程中会不断发出（大约几十毫秒一次）。
    //    isSliderDown() 为 true 表示用户正按着滑条拖动，
    //    这时不要用播放器的位置去覆盖用户拖到的位置，
    //    否则滑条会被“抢回去”，手感很差。
    // ---------------------------------------------------------
    connect(mediaPlayer, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        if (!ui->sliderProgress->isSliderDown())
            ui->sliderProgress->setValue(int(pos / 1000));   // 滑条单位：秒

        // 这里直接更新时间标签；滑条 setValue() 也会触发
        // on_sliderProgress_valueChanged()，两处结果一致，不影响。
        ui->lblCurTime->setText(fmtTime(pos));
    });

    // ---------------------------------------------------------
    // 5. 总时长变化 -> 进度条最大值 + 总时间
    //
    //    一首歌加载完成后才会知道时长（异步），
    //    所以不能像 UI 文件里那样写死 270 秒。
    // ---------------------------------------------------------
    connect(mediaPlayer, &QMediaPlayer::durationChanged, this, [this](qint64 dur) {
        ui->sliderProgress->setMaximum(int(dur / 1000));
        ui->lblTotalTime->setText(fmtTime(dur));
    });

    // ---------------------------------------------------------
    // 6. 用户拖动进度条松手 -> 跳转到对应位置
    //
    //    sliderReleased 只在“松手”时触发一次。
    //    如果用 valueChanged 去 seek，拖动过程中会疯狂调用
    //    setPosition()，既卡顿又不准。
    // ---------------------------------------------------------
    connect(ui->sliderProgress, &QSlider::sliderReleased, this, [this] {
        mediaPlayer->setPosition(qint64(ui->sliderProgress->value()) * 1000);
    });

    // ---------------------------------------------------------
    // 7. 一首播放结束 -> 根据播放模式决定下一步
    //
    //    EndOfMedia 表示音频“自然播放完毕”，
    //    不是用户按暂停，也不是切歌。
    //
    //    手动点“下一首”不受这里影响，那走 on_btnNext_clicked()。
    // ---------------------------------------------------------
    connect(mediaPlayer, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus st) {
        if (st != QMediaPlayer::EndOfMedia || m_files.isEmpty())
            return;

        switch (m_mode) {
        case Mode::SingleLoop:
            // 单曲循环：再播当前这首
            playAt(m_index);
            break;

        case Mode::Sequential:
            // 顺序播放：还有下一首就播，最后一首放完就停
            if (m_index + 1 < m_files.size())
                playAt(m_index + 1);
            break;

        case Mode::LoopAll:
            // 列表循环：playAt 内部取模，最后一首的下一首会回到第 0 首
            playAt(m_index + 1);
            break;

        case Mode::Random:
            // 随机：用 QRandomGenerator 取 [0, size) 的随机下标。
            // 只有一首歌时直接重播；多首时尽量避开当前这首。
            if (m_files.size() > 1) {
                int n = m_index;
                while (n == m_index)
                    n = QRandomGenerator::global()->bounded(int(m_files.size()));
                playAt(n);
            } else {
                playAt(0);
            }
            break;
        }
    });

    updateModeButton();
}

MusicDialog::~MusicDialog()
{
    delete ui;
}

// =============================================================
// 扫描音乐目录
//
//   QDirIterator 的参数：
//     1) 目录
//     2) 文件名过滤器（支持通配符，多个用 QStringList）
//     3) QDir::Files       只返回文件，不返回目录
//     4) QDirIterator::Subdirectories  连子目录一起扫
//
//   扫描结果按迭代顺序存进 m_files，
//   这个顺序就是“顺序播放”的顺序（一般按文件名排序，
//   不额外排序是为了保持文件系统原始顺序）。
// =============================================================
void MusicDialog::scanMusic()
{
    m_files.clear();

    // 板子路径 + 桌面路径；两个都存在时都会扫，重复路径会去重
    QStringList dirs{QStringLiteral("/home/root/Music")};
    const QString homeMusic =
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (!homeMusic.isEmpty() && !dirs.contains(homeMusic))
        dirs << homeMusic;                  // 桌面测试用 ~/Music

    const QStringList filters{"*.mp3", "*.flac", "*.wav", "*.ogg"};
    for (const QString &dir : dirs) {
        if (!QDir(dir).exists())
            continue;

        QDirIterator it(dir, filters, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = it.next();
            if (!m_files.contains(f))       // 去重（两个目录可能重叠）
                m_files << f;
        }
    }

    // 调试输出：确认扫描到的文件名没有乱码（中文名应正常显示）。
    // 如果这里显示 "??.mp3" 或乱码，说明 main.cpp 的 locale 修复没生效。
    qDebug() << "scanMusic: found" << m_files.size() << "files"
             << (m_files.isEmpty() ? QString() : m_files.first());

    if (m_files.isEmpty()) {
        m_index = -1;
        ui->lblSong->setText(QStringLiteral("没有找到音乐"));
        ui->lblArtist->setText(QStringLiteral("请把歌曲放到 /home/root/Music"));
    } else {
        // 还没播放，先把界面上显示成第一首
        ui->lblSong->setText(QFileInfo(m_files.first()).completeBaseName());
        ui->lblArtist->setText(QStringLiteral("本地音乐"));
    }
}

// =============================================================
// 播放第 index 首
//
//   下标取模的作用：
//     playAt(-1)          -> 最后一首（上一首）
//     playAt(size)        -> 第 0 首（下一首）
//     playAt(size + 5)    -> 第 5 首
//   C++ 的 % 对负数结果是负的，所以先加 size 再取模。
//
//   QUrl::fromLocalFile() 把本地路径转成 file:// URL。
//   中文文件名是否正常，取决于 main.cpp 里的 locale/codec 设置。
// =============================================================
void MusicDialog::playAt(int index)
{
    if (m_files.isEmpty())
        return;

    m_index = (index % m_files.size() + m_files.size()) % m_files.size();
    const QUrl url = QUrl::fromLocalFile(m_files.at(m_index));

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    mediaPlayer->setSource(url);                 // Qt6：直接给 QUrl
#else
    mediaPlayer->setMedia(QMediaContent(url));   // Qt5：包一层 QMediaContent
#endif
    mediaPlayer->play();

    // 优先显示 ID3 标签里的标题/歌手，没有标签就用文件名兜底。
    // 注意：标签是文件加载后异步解析的，刚 setMedia 时可能还读不到，
    // 所以这里先用文件名，读到标签再覆盖；读不到也不影响播放。
    const QFileInfo fi(m_files.at(m_index));
    QString title = fi.completeBaseName();
    QString artist = QStringLiteral("本地音乐");

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QMediaMetaData md = mediaPlayer->metaData();
    const QString t = md.value(QMediaMetaData::Title).toString();
    QString a = md.value(QMediaMetaData::ContributingArtist).toString();
    if (a.isEmpty())
        a = md.value(QMediaMetaData::Author).toString();
    if (!t.isEmpty())
        title = t;
    if (!a.isEmpty())
        artist = a;
#else
    const QString t = mediaPlayer->metaData(QStringLiteral("Title")).toString();
    const QString a = mediaPlayer->metaData(QStringLiteral("Author")).toString();
    if (!t.isEmpty())
        title = t;
    if (!a.isEmpty())
        artist = a;
#endif

    ui->lblSong->setText(title);
    ui->lblArtist->setText(artist);
}

// 下一首：交给 playAt 处理越界（最后一首会回到第一首）
void MusicDialog::nextSong()
{
    if (!m_files.isEmpty())
        playAt(m_index + 1);
}

// 上一首：同理，第一首的上一首是最后一首
void MusicDialog::prevSong()
{
    if (!m_files.isEmpty())
        playAt(m_index - 1);
}

// =============================================================
// 判断当前是否正在播放
//
//   Qt5：state() 返回 StoppedState / PlayingState / PausedState
//   Qt6：改名为 playbackState()，枚举值相同
//   这里封装起来，调用方就不用到处写 #if。
// =============================================================
bool MusicDialog::isPlaying() const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return mediaPlayer->playbackState() == QMediaPlayer::PlayingState;
#else
    return mediaPlayer->state() == QMediaPlayer::PlayingState;
#endif
}

// =============================================================
// 播放/暂停切换
//
//   三种状态要区分：
//     正在播放 -> pause()
//     暂停中   -> play()  继续
//     还没加载 -> playAt() 从当前下标或第 0 首开始
//
//   “还没加载”的判断：
//     Qt6 看 source() 是否为空
//     Qt5 看 mediaStatus() 是否 NoMedia
// =============================================================
void MusicDialog::togglePlay()
{
    if (m_files.isEmpty()) {
        ui->lblSong->setText(QStringLiteral("没有找到音乐"));
        return;
    }

    if (isPlaying()) {
        mediaPlayer->pause();
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const bool noMedia = mediaPlayer->source().isEmpty();
#else
    const bool noMedia = mediaPlayer->mediaStatus() == QMediaPlayer::NoMedia;
#endif

    if (noMedia)
        playAt(m_index < 0 ? 0 : m_index);
    else
        mediaPlayer->play();
}

// =============================================================
// 音量滑条 -> 播放器音量
//
//   UI 上的 sliderProgresssound 范围是 0~270，
//   这里换算成 0.0~1.0 的比例，再按平台设置：
//     Qt6：QAudioOutput::setVolume(0.0 ~ 1.0)
//     Qt5：QMediaPlayer::setVolume(0 ~ 100)
//
//   重要：这只改“本应用”的音量，和系统音量是两层相乘：
//
//       最终响度 = 应用音量 × 系统音量
//
//   系统音量（蓝牙耳机、板载输出）用 pactl 调，
//   应用音量用这个滑条调，两者互不替代。
// =============================================================
void MusicDialog::applyVolume(int sliderValue)
{
    const int maxv = qMax(1, ui->sliderProgresssound->maximum());
    const qreal ratio = qBound(0.0, qreal(sliderValue) / qreal(maxv), 1.0);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (audioOutput)
        audioOutput->setVolume(ratio);          // Qt6：0.0 ~ 1.0
#else
    mediaPlayer->setVolume(int(ratio * 100));   // Qt5：0 ~ 100
#endif
}

// 把当前播放模式写到按钮文字上
void MusicDialog::updateModeButton()
{
    switch (m_mode) {
    case Mode::Sequential: ui->btnPlaymode->setText(QStringLiteral("顺序"));     break;
    case Mode::LoopAll:    ui->btnPlaymode->setText(QStringLiteral("列表循环")); break;
    case Mode::SingleLoop: ui->btnPlaymode->setText(QStringLiteral("单曲循环")); break;
    case Mode::Random:     ui->btnPlaymode->setText(QStringLiteral("随机"));     break;
    }
}

// 毫秒 -> "分:秒"
//   秒固定两位：1:05、12:03；分钟不补零。
//   QMediaPlayer 的所有时间单位都是毫秒（qint64）。
QString MusicDialog::fmtTime(qint64 ms)
{
    if (ms < 0)
        ms = 0;
    const qint64 s = ms / 1000;
    return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

// =============================================================
// 下面是 UI 槽函数
//
//   命名规则 on_<objectName>_<signal>() 是 Qt 的“自动连接”约定：
//   ui->setupUi() 里的 QMetaObject::connectSlotsByName() 会按名字
//   自动把 btnPlay 的 clicked 信号连到 on_btnPlay_clicked()。
//   所以这些函数不用手写 connect，但名字一个字都不能错。
// =============================================================

void MusicDialog::on_btnBack_clicked()
{
    this->close();
}

// 进度条数值变化（单位：秒）-> 显示当前时间。
// positionChanged 里 setValue() 也会触发这里，保持时间标签同步。
void MusicDialog::on_sliderProgress_valueChanged(int value)
{
    ui->lblCurTime->setText(fmtTime(qint64(value) * 1000));
}

void MusicDialog::on_btnPlay_clicked()
{
    togglePlay();
}

void MusicDialog::on_btnPrevText_clicked()
{
    prevSong();
}

// 每点一次按“顺序 -> 列表循环 -> 单曲循环 -> 随机 -> 顺序”轮换
void MusicDialog::on_btnPlaymode_clicked()
{
    switch (m_mode) {
    case Mode::Sequential: m_mode = Mode::LoopAll;    break;
    case Mode::LoopAll:    m_mode = Mode::SingleLoop; break;
    case Mode::SingleLoop: m_mode = Mode::Random;     break;
    case Mode::Random:     m_mode = Mode::Sequential; break;
    }
    updateModeButton();
}

void MusicDialog::on_btnNext_clicked()
{
    nextSong();
}

// 图标版“下一首”和文字版“下一首”是两个按钮，行为一样
void MusicDialog::on_btnNextText_clicked()
{
    nextSong();
}

// 音量滑条变化 -> 设置播放器音量 + 显示百分比。
// 构造函数里的 setValue(135) 也会走到这里，完成初始音量设置。
void MusicDialog::on_sliderProgresssound_valueChanged(int value)
{
    applyVolume(value);

    const int maxv = qMax(1, ui->sliderProgresssound->maximum());
    const int percent = qBound(0, value * 100 / maxv, 100);
    ui->lblCurTimesound->setText(QString("%1%").arg(percent));
}
