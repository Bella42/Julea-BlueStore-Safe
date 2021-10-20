CC = gcc

# define any compile-time flags
CFLAGS = -Wall

# define any directories containing header files other than /usr/include
#
INCLUDES = -I..

# define library paths in addition to /usr/lib
#   if I wanted to include libraries not in /usr/lib I'd specify
#   their path using -Lpath, something like:
LFLAGS = -L..

# define any libraries to link into executable:
#   if I want to link in libraries (libx.so or libx.a) I use the -llibname 
#   option, something like (this will link in libmylib.so and libm.so:
LIBS = -ljulea_bluestore

.PHONY: clean

all:    
		$(CC) $(CFLAGS) $(INCLUDES) -c bluestore_test.c $(LFLAGS) $(LIBS)
		$(CC) $(CFLAGS) $(INCLUDES) bluestore_test.o -o bluestore_test $(LFLAGS) $(LIBS)

clean:
		$(RM) *.o *~ $(MAIN)
