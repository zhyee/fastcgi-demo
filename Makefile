CC=gcc
TARGET=fastcgi

all:
	${CC} fastcgi.c -o ${TARGET}

.PHONY: clean

clean:
	rm -f *.o ${TARGET}
