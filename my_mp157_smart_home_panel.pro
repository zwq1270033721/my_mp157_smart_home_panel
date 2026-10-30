QT       += core gui svg network multimedia serialport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

# ---- 目录约定 ----
#   src/    所有 .cpp / .h
#   ui/     所有 .ui（qmake 与 qt-build-arm.sh 都按 FORMS 路径生成 ui_*.h）
#   res.qrc 保留在根目录（交叉编译脚本只从项目根目录找 *.qrc）
# $$PWD 是给 ui_*.h 用的：提升控件会生成 #include "src/videowidget.h"
INCLUDEPATH += $$PWD $$PWD/src

SOURCES += \
    bluettothworker.cpp \
    src/bluetoothdialog.cpp \
    src/cameradialog.cpp \
    src/devicepaneldialog.cpp \
    src/game.cpp \
    src/homedialog.cpp \
    src/keyboardialog.cpp \
    src/lightdialog.cpp \
    src/lockdialog.cpp \
    src/main.cpp \
    src/mainwindow.cpp \
    src/moondialog.cpp \
    src/musicdialog.cpp \
    src/qqdialog.cpp \
    src/serialport.cpp \
    src/shielddialog.cpp \
    src/softkeyboardbinder.cpp \
    src/sundialog.cpp \
    src/tvdialog.cpp \
    src/videowidget.cpp \
    src/waterdialog.cpp \
    src/weatherdialog.cpp \
    src/wifidialog.cpp \
    src/wifitool.cpp

HEADERS += \
    bluettothworker.h \
    src/bluetoothdialog.h \
    src/cameradialog.h \
    src/devicepaneldialog.h \
    src/game.h \
    src/homedialog.h \
    src/keyboardialog.h \
    src/lightdialog.h \
    src/lockdialog.h \
    src/mainwindow.h \
    src/moondialog.h \
    src/musicdialog.h \
    src/qqdialog.h \
    src/serialport.h \
    src/shielddialog.h \
    src/softkeyboardbinder.h \
    src/sundialog.h \
    src/tvdialog.h \
    src/videowidget.h \
    src/waterdialog.h \
    src/weatherdialog.h \
    src/wifidialog.h \
    src/wifitool.h

FORMS += \
    ui/BluetoothDialog.ui \
    ui/CameraDialog.ui \
    ui/DevicePanelDialog.ui \
    ui/HomeDialog.ui \
    ui/LightDialog.ui \
    ui/LockDialog.ui \
    ui/MoonDialog.ui \
    ui/MusicDialog.ui \
    ui/QqDialog.ui \
    ui/ShieldDialog.ui \
    ui/SunDialog.ui \
    ui/TvDialog.ui \
    ui/WaterDialog.ui \
    ui/WeatherDialog.ui \
    ui/WifiDialog.ui \
    ui/game.ui \
    ui/keyboardialog.ui \
    ui/mainwindow.ui \
    ui/serialport.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    res.qrc
