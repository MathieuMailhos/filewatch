CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c99

OBJS = filewatch.o
DEPS = filewatch.c filewatch.h
TARGETS = filewatch.o filewatch

ifdef DEBUG
CFLAGS += -g -DDEBUG
endif

filewatch: ${OBJS}
	${CC} -o $@ ${OBJS} 

filewatch.o: ${DEPS}
	${CC} ${CFLAGS} -c ${DEPS}

all: $(TARGETS)

clean:
	rm -f $(TARGETS) *.h.gch *~ 
.PHONY: clean
