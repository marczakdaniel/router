CC=g++
CFLAGS=-std=c++17 -Wall -Wextra

all: main.o router.o 
	$(CC) $(CFLAGS) -o router main.o router.o

main: main.cpp router.h
	$(CC) $(CFLAGS) -c main.cpp -o main.o

router: router.cpp router.h
	$(CC) $(CFLAGS) -c router.cpp -o router.o

clean:
	rm -vf *.o

distclean:
	rm -vf *.o
	rm -vf router