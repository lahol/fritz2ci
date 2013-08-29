LIBDIRS = -L/usr/local/lib `pkg-config --libs glib-2.0 gthread-2.0 json-glib-1.0` `curl-config --libs` `xml2-config --libs`
INCDIRS = -I/usr/include `pkg-config --cflags glib-2.0 json-glib-1.0` `curl-config --cflags` `xml2-config --cflags`

CC = gcc
CFLAGS = -Wall $(COMPILERFLAGS) $(INCDIRS) -O1
LIBS = -lc -lsqlite3

PREFIX ?= /usr

all: ./main.o ./ci2server.o ./fritz.o ./netutils.o ./ci_areacodes.o ./config.o ./logging.o ./dbhandler.o \
	./cidbmessages.o ./cidbconnection.o ./lookup.o ./msn_lookup.o ./daemon.o
	mkdir -p ./bin
	$(CC) $(CFLAGS) -o./bin/fritz2ci \
	main.c \
	ci2server.c \
	fritz.c \
	netutils.c \
	ci_areacodes.c \
	config.c \
	logging.c \
	dbhandler.c \
	cidbmessages.c \
	cidbconnection.c \
	lookup.c \
	msn_lookup.c \
	daemon.c \
	$(LIBDIRS) $(LIBS) -lcinet

clean:
	rm *.o
	
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

install: install-bin
