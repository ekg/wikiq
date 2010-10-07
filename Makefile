all: wikiq

wikiq: wikiq.c
	gcc -O3 wikiq.c -o wikiq -lexpat

clean:
	rm -f wikiq

.PHONY: all clean
