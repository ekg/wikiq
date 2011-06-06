CXXFLAGS = -O3 
CFLAGS = $(CXXFLAGS)
OBJECTS = wikiq.o md5.o disorder.o 

all: wikiq

wikiq: $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -lpcrecpp -lpcre -lexpat -o wikiq

disorder.o: disorder.h
md5.o: md5.h

clean:
	rm -f wikiq $(OBJECTS)

static: $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -static -lpcrecpp -lpcre -lexpat -o wikiq

gprof:
	$(MAKE) CFLAGS=-pg wikiq

.PHONY: all gprof
