QT += widgets network webenginewidgets

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
