QT += widgets network

# 默认不链接 WebEngine：VM 常只装 dev 包、缺 QtWebEngineProcess 运行时，链接会导致启动崩溃。
# 导航默认用系统浏览器打开。需要内嵌地图时再：
#   sudo apt install -y qt6-webengine-dev libqt6webengine6
#   qmake6 CONFIG+=use_webengine charge-client.pro && make -j4
use_webengine {
    qtHaveModule(webenginewidgets) {
        QT += webenginewidgets
        DEFINES += CHARGE_USE_WEBENGINE
    } else {
        warning("CONFIG+=use_webengine set but Qt webenginewidgets module not found")
    }
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
