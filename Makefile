CC=gcc
LINK=gcc
TARGET=terminal
OBJS=term.o env.o
LIBS=-pthread
CFLAGS=-g -Wall -Wextra

all: ${TARGET}

${TARGET}: ${OBJS}
	${CC} -o ${TARGET} ${OBJS} ${LIBS}

.PHONY : clean

clean:
	rm -f ${TARGET} core*
	rm -f *.o corei