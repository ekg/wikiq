CXX = g++
CFLAGS = -O3

all: wikiq

wikiq: wikiq.c
	$(CXX) $(CFLAGS) wikiq.c -o wikiq -lexpat

clean:
	rm -f wikiq

gprof:
	$(MAKE) CFLAGS=-pg wikiq

.PHONY: all gprof
