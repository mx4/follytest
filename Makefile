OS=$(shell uname -s)

CC = clang++
LINK = clang++
PROGRAM = test
OBJECTS = $(PROGRAM).o
CFLAGS = -std=c++1y -fno-omit-frame-pointer -g

LIBS_COMMON = -lfolly -lglog
LIBS_LINUX = -lboost_context -lpthread -latomic
LIBS_MAC = -lboost_context-mt

ifeq ($(OS), Linux)
LIBS = $(LIBS_COMMON) $(LIBS_LINUX)
endif

ifeq ($(OS), Darwin)
LIBS = $(LIBS_COMMON) $(LIBS_MAC)
endif

all : $(PROGRAM)

$(PROGRAM) : $(OBJECTS)
	$(CC) -o $(PROGRAM) $(OBJECTS) $(LIBS)

.cpp.o:
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(PROGRAM) *.o *~
