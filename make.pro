TEMPLATE = app
CONFIG +=  console warn_on
QMAKE_CXXFLAGS_DEBUG += -Wall
QMAKE_CXXFLAGS_RELEASE += -Wall

HEADERS =\
        civetweb/civetweb.h\
        civetweb/civetserver.h

SOURCES =\
        main.cpp\
        civetweb/civetweb.c\
        civetweb/civetserver.cpp




TARGET = rvs_help

LIBS +=\
     
