#############################################################################
# Makefile for ma
#############################################################################

####### Compiler, tools and options

CC            = gcc
DEFINES       = 
CFLAGS        = -g -Wall -W $(DEFINES)
CXXFLAGS      = -g -Wall -W $(DEFINES)
INCPATH       = -I./defines -I.
LINK          = gcc
LFLAGS        = -lpthread -lm
LIBS          = $(SUBLIBS)    
TAR           = tar -cf
COMPRESS      = gzip -9f
COPY          = cp -f
SED           = sed
COPY_FILE     = $(COPY)
COPY_DIR      = $(COPY) -r
STRIP         = strip
INSTALL_FILE  = install -m 644 -p
INSTALL_DIR   = $(COPY_DIR)
INSTALL_PROGRAM = install -m 755 -p
DEL_FILE      = rm -f
SYMLINK       = ln -f -s
DEL_DIR       = rmdir
MOVE          = mv -f
CHK_DIR_EXISTS= test -d
MKDIR         = mkdir -p

####### Output directory

OBJECTS_DIR   = ./

####### Files

SOURCES       = ./blxer.c 
OBJECTS       = ma.o
TARGET        = ma

first: all
####### Implicit rules

.SUFFIXES: .o .c .C

.C.o:
	$(CXX) -c $(CXXFLAGS) $(INCPATH) -o "$@" "$<"

.c.o:
	$(CC) -c $(CFLAGS) $(INCPATH) -o "$@" "$<"

####### Build rules

all: Makefile $(TARGET)

$(TARGET):  $(OBJECTS)  
	$(LINK) $(LFLAGS) -o $(TARGET) $(OBJECTS) $(OBJCOMP) $(LIBS)

clean:
	-$(DEL_FILE) $(OBJECTS)

####### Sub-libraries

distclean: clean
	-$(DEL_FILE) $(TARGET) 
	-$(DEL_FILE) Makefile

####### Compile

ma.o: ./blxer.c 
	$(CC) -c $(CFLAGS) $(INCPATH) -o ma.o ./blxer.c

