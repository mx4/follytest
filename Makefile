OS=$(shell uname -s)

CC = clang++
LINK = clang++
CFLAGS = -std=c++1y -fno-omit-frame-pointer -g

LIBS_COMMON = -lfolly -lglog
LIBS_LNX = -lboost_context -lpthread -latomic
LIBS_MAC = -lboost_context-mt

ifeq ($(OS), Linux)
LIBS = $(LIBS_COMMON) $(LIBS_LNX)
endif

ifeq ($(OS), Darwin)
LIBS = $(LIBS_COMMON) $(LIBS_MAC)
endif

SRC = multi.cpp ping_pong.cpp
OBJ = $(SRC:.cpp=.o)
BIN = $(OBJ:.o=)

all : $(BIN)

$(OBJ): $(SRC)

%: %.o
	$(LINK) -o $@ $(LIBS) $<

.cpp.o:
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(BIN) *.o *~
