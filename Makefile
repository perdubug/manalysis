IDIR   =./include
CC     = gcc
CFLAGS = -I$(IDIR)

LIBS   = -lm -pthread

_DEPS = ma.h
DEPS  = $(patsubst %,$(IDIR)/%,$(_DEPS))

_OBJ  = ma_lib.o main.o
OBJ   = $(patsubst %,$(ODIR)/%,$(_OBJ))

ma:
	gcc -Wall -o ma main.c ma_lib.c $(CFLAGS) $(LIBS)

.PHONY: clean

clean:
	rm -f ma *.o *~ core $(INCDIR)/*~ 

