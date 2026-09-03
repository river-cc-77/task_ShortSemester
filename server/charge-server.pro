QT += core network sql

CONFIG += console c++17
CONFIG -= app_bundle

TARGET = charge-server

SOURCES += \
    main.cpp \
    tcpserver.cpp \
    clienthandler.cpp \
    protocol.cpp \
    authmanager.cpp \
    dbmanager.cpp \
    handlers/pinghandler.cpp \
    handlers/userhandler.cpp \
    handlers/adminhandler.cpp \
    handlers/stationhandler.cpp

HEADERS += \
    tcpserver.h \
    clienthandler.h \
    protocol.h \
    authmanager.h \
    dbmanager.h \
    handlers/pinghandler.h \
    handlers/userhandler.h \
    handlers/adminhandler.h \
    handlers/stationhandler.h
