#include "mainwindow.h"
#include <QDebug>

#include <QApplication>
#include <clocale>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QTextCodec>
#endif

// ============================================================
// 程序入口
//
// 这里做了一件和 UI 看似无关、但对“中文歌名能不能播放”非常关键的事：
// 在创建 QApplication 之前，把 locale 和 Qt 的文件名编解码器
// 强制设为 UTF-8。
//
// 背景（MP157 板子实测）：
//   板子环境里 LANG 为空，locale 是 POSIX/C。
//   Qt5 在 C locale 下，QTextCodec::codecForLocale() 会退化成 Latin-1，
//   QDirIterator / QFile 用 Latin-1 去解码 UTF-8 文件名：
//
//       "于是.mp3" 的 UTF-8 字节：E4 BA 8E E6 98 AF
//       按 Latin-1 解码后变成：    "äºŽæ˜¯.mp3"（乱码）
//
//   程序再把这个乱码路径交给 QMediaPlayer/GStreamer：
//       GStreamer; Unable to play - ".../<乱码>.mp3"
//       Error: "Resource not found."
//
//   结果就是：点播放没有任何反应，PulseAudio 里连音频流都不会创建。
//   注意：命令行 mpg123/gst-launch 不受影响，因为它们不经过 Qt 的
//   文件名 codec，直接使用 shell 传进来的原始字节。
//
// 修复分两层，缺一不可：
//   1) setlocale(LC_ALL, "en_US.utf8")
//      -> 影响 glib/GStreamer 那一侧的文件路径编码
//   2) QTextCodec::setCodecForLocale(UTF-8)   [仅 Qt5 需要]
//      -> 影响 Qt 自己的 QDirIterator/QFile 解码文件名
//   只做第 1 步不够：Qt5 的文件名 codec 仍然是 Latin-1。
//
// Qt6 不需要第 2 步：Qt6 在 Unix 下文件名固定按 UTF-8 处理，
// 而且 QTextCodec 已经移出 QtCore（需要 Core5Compat 才有）。
// ============================================================

int main(int argc, char *argv[])
{
    // 第 1 层：把进程 locale 切到 UTF-8。
    // 如果系统已经有 LANG/LC_ALL，就尊重用户设置，用空字符串让 C 库去解析。
    if (qEnvironmentVariableIsEmpty("LANG") && qEnvironmentVariableIsEmpty("LC_ALL")) {
        qputenv("LANG", "en_US.utf8");                  // 板子上存在的 locale
        if (!std::setlocale(LC_ALL, "en_US.utf8"))      // 失败再退到 C.UTF-8
            std::setlocale(LC_ALL, "C.UTF-8");
    } else {
        std::setlocale(LC_ALL, "");
    }

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // 第 2 层：Qt5 专用。强制 Qt 用 UTF-8 读写文件名，
    // 这样 QDirIterator 扫描 /home/root/Music 时中文名不会变成乱码。
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
#endif

    // 调试输出：启动日志里应看到 "locale = en_US.utf8"（或 C.UTF-8）。
    qDebug() << "locale =" << std::setlocale(LC_ALL, nullptr);

    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
