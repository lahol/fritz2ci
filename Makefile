LIBDIRS = -L/usr/local/lib `pkg-config --libs glib-2.0 gthread-2.0` `curl-config --libs` `xml2-config --libs`
INCDIRS = -I/usr/include `pkg-config --cflags glib-2.0` `curl-config --cflags` `xml2-config --cflags`

CC = gcc
CFLAGS = -Wall $(COMPILERFLAGS) $(INCDIRS) -O1
LIBS = -lc -lpthread -lsqlite3

all: ./main.o ./ci2server.o ./fritz.o ./netutils.o ./ci_areacodes.o ./config.o ./logging.o ./dbhandler.o \
	./cidbmessages.o ./cidbconnection.o ./lookup.o ./msn_lookup.o
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
	$(LIBDIRS) $(LIBS)

clean:
	rm *.o
	
install:
	mkdir -p /var/callerinfo
	mkdir -p /usr/share/callerinfo
	cp ./bin/fritz2ci /usr/local/bin
	cp fritz2ci-base.conf /etc/fritz2ci.conf
	cp ./share/* /usr/share/callerinfo
	cp ./bin/scripts/fritz2ci.conf /etc/init