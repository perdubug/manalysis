IDIR   =./include
CC     = gcc
CFLAGS = -I$(IDIR)

LIBS   = -lm -pthread

_DEPS = ma.h thread_pool.h
DEPS  = $(patsubst %,$(IDIR)/%,$(_DEPS))

_OBJ  = ma_lib.o thread_pool.o main.o
OBJ   = $(patsubst %,$(ODIR)/%,$(_OBJ))

all: main

threadpool.o : thread_pool.c $(IDIR)/thread_pool.h
	gcc -g -S -Wall thread_pool.c -I$(IDIR)
	gcc -g -c thread_pool.s

ma_lib.o : ma_lib.c $(IDIR)/ma.h
	gcc -g -S -Wall ma_lib.c  -I$(IDIR)
	gcc -g -c ma_lib.s

main.o : main.c $(IDIR)/ma.h $(IDIR)/thread_pool.h
	gcc -g -S -Wall main.c  -I$(IDIR)
	gcc -g -c main.s
		
main: thread_pool.o ma_lib.o main.o
	gcc -Wall thread_pool.o ma_lib.o main.o $(CFLAGS) $(LIBS) -o ma 

.PHONY: clean

clean:
	rm -f ma *.o *.s *~ core $(INCDIR)/*~ 

