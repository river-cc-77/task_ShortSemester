# ads-collector: 大屏数据定时采集聚合服务（Qt6 / C++ 控制台程序）
QT += core sql

CONFIG += console c++17
CONFIG -= app_bundle

TARGET = ads-collector

SOURCES += \
    main.cpp \
    adsdatabase.cpp \
    aggregator.cpp \
    clean.cpp

HEADERS += \
    adsdatabase.h \
    aggregator.h \
    clean.h
