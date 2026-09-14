QT += core gui widgets printsupport network

# 已安装 Qt WebEngine 时在管理端内嵌 dashboard 大屏
qtHaveModule(webenginewidgets) {
    QT += webenginewidgets
    DEFINES += CHARGE_USE_WEBENGINE
}

CONFIG += c++17
CONFIG -= app_bundle
INCLUDEPATH += ../client
TARGET = charge-admin
RESOURCES += charge-admin.qrc
SOURCES += \
    main.cpp \
    apiclient.cpp \
    loginwindow.cpp \
    mainwindow.cpp \
    pileeditdialog.cpp \
    pilestatusdialog.cpp \
    pileadddialog.cpp \
    orderdetaildialog.cpp \
    stationeditdialog.cpp \
    stationdetaildialog.cpp \
    announcementeditdialog.cpp \
    qcustomplot.cpp
HEADERS += \
    apiclient.h \
    loginwindow.h \
    mainwindow.h \
    pileeditdialog.h \
    pilestatusdialog.h \
    pileadddialog.h \
    orderdetaildialog.h \
    stationeditdialog.h \
    stationdetaildialog.h \
    announcementeditdialog.h \
    qcustomplot.h
FORMS += \
    mainwindow.ui \
    pileeditdialog.ui \
    pilestatusdialog.ui \
    pileadddialog.ui \
    orderdetaildialog.ui \
    stationeditdialog.ui \
    stationdetaildialog.ui
