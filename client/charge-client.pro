QT += widgets network

CONFIG += c++17
CONFIG -= app_bundle

TARGET = charge-client

SOURCES += \
    loginwindow.cpp \
    main.cpp \
    apiclient.cpp \
    mainwindow.cpp

HEADERS += \
    apiclient.h \
    loginwindow.h \
    mainwindow.h

FORMS += \
    loginwindow.ui

RESOURCES += \
    resources.qrc
