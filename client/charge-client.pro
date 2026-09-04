QT += widgets network

CONFIG += c++17
CONFIG -= app_bundle

TARGET = charge-client

SOURCES += \
    main.cpp \
    apiclient.cpp \
    mainwindow.cpp

HEADERS += \
    apiclient.h \
    mainwindow.h
