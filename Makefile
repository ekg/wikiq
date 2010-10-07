CXX = g++
CFLAGS = -O3

all: wikiq

wikiq: wikiq.c
	$(CXX) $(CFLAGS) wikiq.c -o wikiq -lexpat

clean:
	rm -f wikiq

.PHONY: all
