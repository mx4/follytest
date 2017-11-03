OS=$(shell uname -s)

CXX = clang++
LD = clang++
CXXFLAGS = -std=c++14 -fno-omit-frame-pointer -g

LIBS_COMMON = -lfolly -lglog
LIBS_Linux  = -lboost_context -lpthread -latomic
LIBS_Darwin = -lboost_context-mt

LDLIBS = $(LIBS_COMMON) $(LIBS_$(OS))

SRC = multi.cpp ping_pong.cpp
OBJ = $(SRC:.cpp=.o)
BIN = $(OBJ:.o=)

all : $(BIN)
$(OBJ): $(SRC)

%: %.o
	$(LD) $(LDLIBS) -o $@ $<

.cpp.o:
	$(CXX) $(CXXFLAGS) -c $<

clean:
	rm -f $(BIN) $(OBJ) *~
