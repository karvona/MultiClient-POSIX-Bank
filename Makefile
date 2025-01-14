PROGS = build/bank_server build/client
CC = gcc
CFLAGS = -Wall -pedantic -pthread -I./src

all: ${PROGS}

build/bank_server: build/bank_server.o build/bank_helper.o
	${CC} ${CFLAGS} ${LDFLAGS} $^ -o $@

build/client: build/client.o build/bank_helper.o
	${CC} ${CFLAGS} ${LDFLAGS} $^ -o $@

build/bank_server.o: src/bank_server.c src/bank_helper.h
	${CC} ${CFLAGS} -c $< -o $@

build/client.o: src/client.c src/bank_helper.h
	${CC} ${CFLAGS} -c $< -o $@

build/bank_helper.o: src/bank_helper.c src/bank_helper.h
	${CC} ${CFLAGS} -c $< -o $@

clean:
	rm -f ${PROGS} build/*.o *~
