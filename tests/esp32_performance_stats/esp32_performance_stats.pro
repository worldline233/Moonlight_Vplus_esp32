TEMPLATE = app
TARGET = esp32_performance_stats_test
CONFIG += console c++17
CONFIG -= app_bundle qt
SOURCES += main.cpp
HEADERS += ../../app/streaming/input/esp32performancestats.h ../../app/streaming/input/esp32inputstatus.h
