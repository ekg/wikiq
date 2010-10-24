CXX = g++
CFLAGS = -O3
OBJECTS = disorder.o

all: wikiq

wikiq: wikiq.c $(OBJECTS)
	$(CXX) $(CFLAGS) wikiq.c $(OBJECTS) -o wikiq -lexpat

disorder.o: disorder.c
	$(CXX) $(CFLAGS) -c disorder.c

clean:
	rm -f wikiq $(OBJECTS)

gprof:
	$(MAKE) CFLAGS=-pg wikiq

.PHONY: all gprof
