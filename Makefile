##
## Memlockd Makefile
##

CC = gcc

LIBEVENT_PATH = /usr/local

INCLUDE = -I./ -I$(LIBEVENT_PATH)/include
LIBRARY = -L$(LIBEVENT_PATH)/lib -Wl,-R$(LIBEVENT_PATH)/lib -levent -lpthread -lm

CFLAGS = -g -Wall -DMDEBUG $(INCLUDE)

objects = hashtable.o hash.o daemon.o \
		  socket.o conn.o item.o common.o

progbin = memlockd

all: $(objects) $(progbin) 

%.o:%.c
	$(CC) $(CFLAGS) -c $<

$(progbin): memlockd.c $(objects)
	$(CC) $(CFLAGS) $(LIBRARY) -o $(progbin) memlockd.c $(objects)

.PHONY: clean
clean:
	-rm *.o memlockd

