all: wikiq

wikiq: wikiq.c
	gcc wikiq.c -o wikiq -lexpat

.PHONY: all
