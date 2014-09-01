CC=gcc
CFLAGS=-Wall -Wextra -Werror -std=c99
LDFLAGS=-lpthread

DEPS = common.h sync.h io.h pp.h job.h messaging.h xcalls.h time.h option.h array_list.h found_a_bug.h
OBJ = main.o io.o pp.o job.o messaging.o time.o option.o found_a_bug.o

all: landslide-id

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

landslide-id: $(OBJ)
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

.PHONY: clean

clean:
	rm -f *.o landslide-id
