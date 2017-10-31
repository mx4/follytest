CC = clang++
LINK = clang++
PROGRAM = test
OBJECTS = $(PROGRAM).o
CFLAGS = -std=c++1y -fno-omit-frame-pointer -g
INCLUDE =
LDFLAGS =
LIBS = -lfolly -lglog -lfollybenchmark -lboost_context-mt

all : $(PROGRAM)

$(PROGRAM) : $(OBJECTS)
	$(CC) $(LDFLAGS) -o $(PROGRAM) $(OBJECTS) $(LIBS)

.cpp.o:
	$(CC) $(CFLAGS) $(INCLUDE) -c $<

clean:
	rm -f $(PROGRAM) *.o *~
run:
	LD_LIBRARY_PATH=/src/git/folly/folly/.libs ./test


