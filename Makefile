CC=g++

debugger: linenoise.o main.cpp linenoise.h
	$(CC) -g -o debugger main.cpp linenoise.o -I .
linenoise.o: linenoise.h linenoise.c
	gcc -c linenoise.c
