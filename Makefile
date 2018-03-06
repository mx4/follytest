OS=$(shell uname -s)

CXX = g++
LD  = g++
CXXFLAGS = -std=c++14 -fno-omit-frame-pointer -g -Wall

LIBS_COMMON = -lfolly -lglog
LIBS_Linux  = -lboost_context -lpthread -latomic
LIBS_Darwin = -lboost_context-mt

LDLIBS = $(LIBS_COMMON) $(LIBS_$(OS))

SRC = $(shell find . -name "*.cpp")
OBJ = $(SRC:.cpp=.o)
BIN = $(OBJ:.o=)

all : $(BIN)
$(OBJ): $(SRC)

%: %.o
	$(LD) -o $@ $< $(LDLIBS)

.cpp.o:
	$(CXX) $(CXXFLAGS) -c $<

clean:
	rm -f $(BIN) $(OBJ) *~
