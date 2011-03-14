CXX = g++
CFLAGS = -O3
OBJECTS = disorder.o md5.o

all: wikiq

wikiq: wikiq.cpp $(OBJECTS)
	$(CXX) $(CFLAGS) -lpcrecpp -lexpat wikiq.cpp $(OBJECTS) -o wikiq

disorder.o: disorder.c disorder.h
	$(CXX) $(CFLAGS) -c disorder.c

md5.o: md5.c md5.h
	$(CXX) $(CFLAGS) -c md5.c -lm

clean:
	rm -f wikiq $(OBJECTS)

gprof:
	$(MAKE) CFLAGS=-pg wikiq

.PHONY: all gprof
