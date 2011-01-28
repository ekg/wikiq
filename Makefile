CXX = g++
CFLAGS = -O3
OBJECTS = disorder.o md5.o

all: wikiq

wikiq: wikiq.c $(OBJECTS)
	$(CXX) $(CFLAGS) wikiq.c $(OBJECTS) -o wikiq -lexpat

disorder.o: disorder.c disorder.h
	$(CXX) $(CFLAGS) -c disorder.c

md5.o: md5.c md5.h
	$(CXX) $(CFLAGS) -c md5.c -lm

clean:
	rm -f wikiq $(OBJECTS)

gprof:
	$(MAKE) CFLAGS=-pg wikiq

.PHONY: all gprof
