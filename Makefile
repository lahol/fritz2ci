LIBDIRS = -L/usr/local/lib `pkg-config --libs glib-2.0 gthread-2.0 json-glib-1.0` `curl-config --libs` `xml2-config --libs`
INCDIRS = -I/usr/include `pkg-config --cflags glib-2.0 json-glib-1.0` `curl-config --cflags` `xml2-config --cflags`

CC = gcc
CFLAGS = -Wall $(COMPILERFLAGS) $(INCDIRS) -O1
LIBS = -lc -lsqlite3 -lcinet

PREFIX ?= /usr

ci_SRC := $(wildcard *.c)
ci_OBJ := $(ci_SRC:.c=.o)
ci_HEADERS := $(wildcard *.h)

all: fritz2ci

fritz2ci: $(ci_OBJ)
	mkdir -p ./bin
	$(CC) $(CFLAGS) -o./bin/fritz2ci $^ $(LIBDIRS) $(LIBS)

%.o: %.c $(ci_HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm *.o ./bin/fritz2ci
	
install-bin:
	mkdir -p /var/callerinfo
	install ./bin/fritz2ci ${PREFIX}/bin
#	cp fritz2ci-base.conf /etc/fritz2ci.conf
#	cp ./share/* /usr/share/callerinfo
#	cp ./bin/scripts/fritz2ci.conf /etc/init

install-data:
	mkdir -p /usr/share/callerinfo
	cp ./share/revlookup.xml /usr/share/callerinfo/
	cp ./share/vorwahl.dat /usr/share/callerinfo/

install-all: install-bin install-data

install: install-all

.PHONY: all clean install
