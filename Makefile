##
## Memlockd Makefile
##

CC = gcc

INCLUDE = -I./ -I/usr/local/eyou/mail/opt/include
LIBRARY = -L/usr/local/eyou/mail/opt/lib -Wl,-R/usr/local/eyou/mail/opt/lib -levent -lpthread -lm

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

