QT += widgets network

# 已安装 Qt WebEngine 时自动启用应用内交互地图（须 libqt6webenginecore6-bin 等运行时）
qtHaveModule(webenginewidgets) {
    QT += webenginewidgets
    DEFINES += CHARGE_USE_WEBENGINE
}

CONFIG += c++17
CONFIG -= app_bundle

TARGET = charge-client

SOURCES += \
    loginwindow.cpp \
    main.cpp \
    apiclient.cpp \
    mainwindow.cpp \
    mapnavigationdialog.cpp

HEADERS += \
    apiclient.h \
    loginwindow.h \
    mainwindow.h \
    mapnavigationdialog.h

RESOURCES += \
    resources.qrc
