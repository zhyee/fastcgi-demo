all:
	gcc fastcgi.c -o fastcgi

.PHONY: clean

clean:
	rm -f fastcgi
