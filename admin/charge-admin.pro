QT += widgets network

CONFIG += c++17
CONFIG -= app_bundle

TARGET = charge-admin

SOURCES += \
    main.cpp \
    apiclient.cpp \
    loginwindow.cpp \
    mainwindow.cpp

HEADERS += \
    apiclient.h \
    loginwindow.h \
    mainwindow.h

FORMS += \
    mainwindow.ui
