QT += widgets network

CONFIG += c++17
CONFIG -= app_bundle

TARGET = charge-client

SOURCES += \
    main.cpp \
    apiclient.cpp \
    loginwindow.cpp \
    mainwindow.cpp

HEADERS += \
    apiclient.h \
    loginwindow.h \
    mainwindow.h
