prefix ?= /usr/local
CFLAGS += -Wall

linux-serial-test: linux-serial-test.c setbaudrate.c

install: linux-serial-test
	install linux-serial-test $(prefix)/bin/

clean:
	rm -f linux-serial-test *.o
