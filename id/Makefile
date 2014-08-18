CC=gcc
CFLAGS=-Wall -Wextra -Werror -std=c99
LDFLAGS=-lpthread

DEPS = common.h io.h pp.h job.h
OBJ = main.o io.o pp.o job.o

all: landslide-id

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

landslide-id: $(OBJ)
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

.PHONY: clean

clean:
	rm -f *.o landslide-id
