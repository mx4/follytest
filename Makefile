OS=$(shell uname -s)

CC = clang++
LINK = clang++
CFLAGS = -std=c++1y -fno-omit-frame-pointer -g -O2

LIBS_COMMON = -lfolly -lglog
LIBS_Linux  = -lboost_context -lpthread -latomic
LIBS_Darwin = -lboost_context-mt

LIBS = $(LIBS_COMMON) $(LIBS_$(OS))

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
