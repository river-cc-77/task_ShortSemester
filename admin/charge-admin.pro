QT += core gui widgets printsupport network
CONFIG += c++17
CONFIG -= app_bundle
TARGET = charge-admin
SOURCES += \
    main.cpp \
    apiclient.cpp \
    loginwindow.cpp \
    mainwindow.cpp \
    pileeditdialog.cpp \
    qcustomplot.cpp
HEADERS += \
    apiclient.h \
    loginwindow.h \
    mainwindow.h \
    pileeditdialog.h \
    qcustomplot.h
FORMS += \
    mainwindow.ui \
    pileeditdialog.ui
