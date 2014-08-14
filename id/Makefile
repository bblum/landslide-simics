CC=gcc
CFLAGS=-Wall -Wextra -Werror -std=c99

DEPS = common.h
OBJ = main.o

all: landslide-id

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

landslide-id: $(OBJ)
	gcc -o $@ $^ $(CFLAGS)

.PHONY: clean

clean:
	rm -f *.o landslide-id
