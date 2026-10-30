#ifndef MUSICDIALOG_H
#define MUSICDIALOG_H

// ============================================================
// MusicDialog —— 本地音乐播放器
//
// 设计思路：
//   1. 启动时扫描音乐目录，把文件路径存进 QStringList m_files
//   2. m_index 记录当前播放到第几首
//   3. QMediaPlayer 只负责“放一个文件”，列表/上一首/下一首/播放模式
//      全部由本类自己维护
//   4. 进度、时长、播放结束都通过 QMediaPlayer 的信号回填到界面
//
// 为什么不用 QMediaPlaylist？
//   Qt5 有 QMediaPlaylist，但 Qt6 已经把它删掉了。
//   本工程要同时编桌面（Qt6）和板子（Qt5.12），
//   所以统一用 QStringList + 下标，两头都能编。
//
// Qt5 / Qt6 的 API 差异（代码里用 #if QT_VERSION 区分）：
//   Qt5: setMedia(QMediaContent)   state()          setVolume(0~100)
//   Qt6: setSource(QUrl)           playbackState()  音量在 QAudioOutput 上
// ============================================================

#include <QDialog>
#include <QMediaPlayer>
#include <QStringList>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
class QAudioOutput;          // Qt6 才需要，cpp 里 include 完整定义
#endif

namespace Ui {
class MusicDialog;
}

class MusicDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MusicDialog(QWidget *parent = nullptr);
    ~MusicDialog();

private slots:
    // 这些 on_xxx_yyy() 是 Qt 的“自动连接”命名规则：
    // 控件 objectName 是 xxx、信号是 yyy，setupUi() 时会自动连上，
    // 不需要手写 connect（按钮的 clicked 信号自动对应 _clicked）。
    void on_btnBack_clicked();

    void on_sliderProgress_valueChanged(int value);

    void on_btnPlay_clicked();

    void on_btnPrevText_clicked();

    void on_btnPlaymode_clicked();

    void on_btnNext_clicked();

    void on_btnNextText_clicked();

    void on_sliderProgresssound_valueChanged(int value);

private:
    // 四种播放模式：顺序 / 列表循环 / 单曲循环 / 随机
    enum class Mode { Sequential, LoopAll, SingleLoop, Random };

    void scanMusic();                 // 扫描目录，填充 m_files
    void playAt(int index);           // 播放第 index 首（自动处理越界）
    void nextSong();                  // 下一首
    void prevSong();                  // 上一首
    void togglePlay();                // 播放/暂停切换
    bool isPlaying() const;           // Qt5/Qt6 判断“正在播放”的封装
    void applyVolume(int sliderValue); // 音量滑条 -> 播放器音量
    void updateModeButton();          // 把当前模式显示到按钮上
    static QString fmtTime(qint64 ms); // 毫秒 -> "分:秒"

    Ui::MusicDialog *ui;
    QMediaPlayer *mediaPlayer = nullptr;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Qt6 的播放器本身没有音量，必须挂一个 QAudioOutput，否则没声音
    QAudioOutput *audioOutput = nullptr;
#endif

    QStringList m_files;      // 扫描到的所有歌曲路径
    int m_index = -1;         // 当前播放下标，-1 表示还没开始
    Mode m_mode = Mode::Sequential;
};

#endif // MUSICDIALOG_H
