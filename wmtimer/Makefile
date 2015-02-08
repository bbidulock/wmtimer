CC     = gcc
LIBDIR = -L/usr/X11R6/lib -L/usr/lib
LIBS   = -lXpm -lXext -lX11 -lpthread `pkg-config --libs gtk+-2.0`
CFLAGS = `pkg-config --cflags gtk+-2.0`

OBJS =	wmtimer.o \
	../wmgeneral/wmgeneral.o \
	../wmgeneral/misc.o \
	../wmgeneral/list.o

all: wmtimer
.c.o:
	$(CC) -g -c -O2 -Wall $< -o $*.o $(CFLAGS)

wmtimer: $(OBJS)
	$(CC) -Wall -g -o wmtimer $^ $(LIBDIR) $(LIBS)

install:
	cp wmtimer /usr/local/bin/

clean:
	rm -f wmtimer $(OBJS)
	rm -f wmtimer
