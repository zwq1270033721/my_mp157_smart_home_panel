QT       += core gui svg network multimedia

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    bluetoothdialog.cpp \
    cameradialog.cpp \
    devicepaneldialog.cpp \
    game.cpp \
    homedialog.cpp \
    keyboardialog.cpp \
    lightdialog.cpp \
    lockdialog.cpp \
    moondialog.cpp \
    musicdialog.cpp \
    qqdialog.cpp \
    serialport.cpp \
    shielddialog.cpp \
    softkeyboardbinder.cpp \
    sundialog.cpp \
    tvdialog.cpp \
    videowidget.cpp \
    waterdialog.cpp \
    weatherdialog.cpp \
    wifidialog.cpp \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    bluetoothdialog.h \
    cameradialog.h \
    devicepaneldialog.h \
    game.h \
    homedialog.h \
    keyboardialog.h \
    lightdialog.h \
    lockdialog.h \
    moondialog.h \
    musicdialog.h \
    qqdialog.h \
    serialport.h \
    shielddialog.h \
    softkeyboardbinder.h \
    sundialog.h \
    tvdialog.h \
    videowidget.h \
    waterdialog.h \
    weatherdialog.h \
    wifidialog.h \
    mainwindow.h

FORMS += \
    BluetoothDialog.ui \
    CameraDialog.ui \
    DevicePanelDialog.ui \
    HomeDialog.ui \
    LightDialog.ui \
    LockDialog.ui \
    MoonDialog.ui \
    MusicDialog.ui \
    QqDialog.ui \
    ShieldDialog.ui \
    SunDialog.ui \
    TvDialog.ui \
    WaterDialog.ui \
    WeatherDialog.ui \
    WifiDialog.ui \
    game.ui \
    keyboardialog.ui \
    mainwindow.ui \
    serialport.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    res.qrc
