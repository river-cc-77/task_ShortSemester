QT += widgets network

# WebEngine 可选：未安装 qt6-webengine-dev 时仍可编译，导航会回退为系统浏览器
qtHaveModule(webenginewidgets): QT += webenginewidgets

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
