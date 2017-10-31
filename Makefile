OS=$(shell uname -s)

CC = clang++
LINK = clang++
PROGRAM = test
OBJECTS = $(PROGRAM).o
CFLAGS = -std=c++1y -fno-omit-frame-pointer -g

LIBS_COMMON = -lfolly -lglog

FOLLY_PATH_LINUX = /src/git/folly/folly/.libs
INCLUDE_LINUX = -I/src/git/folly/
LDFLAGS_LINUX = -L$(FOLLY_PATH_LINUX)
LIBS_LINUX = -lboost_context -lpthread

INCLUDE_MAC =
LDFLAGS_MAC =
LIBS_MAC = -lboost_context-mt

ifeq ($(OS), Linux)
INCLUDE = $(INCLUDE_LINUX)
LDFLAGS = $(LDFLAGS_LINUX)
LIBS = $(LIBS_COMMON) $(LIBS_LINUX)
endif

ifeq ($(OS), Darwin)
INCLUDE = $(INCLUDE_MAC)
LDFLAGS = $(LDFLAGS_MAC)
LIBS = $(LIBS_COMMON) $(LIBS_MAC)
endif

all : $(PROGRAM)

$(PROGRAM) : $(OBJECTS)
	$(CC) $(LDFLAGS) -o $(PROGRAM) $(OBJECTS) $(LIBS)

.cpp.o:
	$(CC) $(CFLAGS) $(INCLUDE) -c $<

clean:
	rm -f $(PROGRAM) *.o *~
run:
	LD_LIBRARY_PATH=$(FOLLY_PATH_LINUX) ./test

vgrun:
	LD_LIBRARY_PATH=$(FOLLY_PATH_LINUX) valgrind ./test
