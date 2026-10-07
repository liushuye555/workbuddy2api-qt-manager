QT += widgets network

CONFIG += c++17 release windows
TEMPLATE = app
TARGET = workbuddy2api-manager

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    servicecontrol.cpp \
    configstore.cpp \
    logdiagnostics.cpp \
    taskrunner.cpp

HEADERS += \
    mainwindow.h \
    servicecontrol.h \
    configstore.h \
    logdiagnostics.h \
    taskrunner.h

# 最终程序放在项目根目录，和 config.json、start.ps1、stop.ps1 同级，
# 这样用户双击程序时无需额外配置工作目录。
DESTDIR = $$PWD/..

QMAKE_CXXFLAGS += -Wall -Wextra
