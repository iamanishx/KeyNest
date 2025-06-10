CC=gcc
CFLAGS=-Wall -Werror
LIBS=-lpthread -lmicrohttpd -ljson-c

all: c-db

c-db: main.o Engines/engine.o server/server.o server/handler.o
	$(CC) $^ -o $@ $(LIBS)

main.o: main.c
	$(CC) $(CFLAGS) -c $< -o $@

Engines/engine.o: Engines/engine.c
	$(CC) $(CFLAGS) -c $< -o $@ 
	
server/server.o: server/server.c
	$(CC) $(CFLAGS) -c $< -o $@

server/handler.o: server/handler.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o Engines/*.o server/*.o c-db