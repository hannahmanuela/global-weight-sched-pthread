CC=gcc
CFLAGS=-I. -O2 -g
DEPS = global_heap.h group.h heap.h lheap.h mheap.h util.h  ticks.h
OBJ = global_heap.o group.o mheap.o lheap.o heap.o util.c ticks.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $(CFLAGS) $<

all: global-heap test-heap test-worst

global-heap: driver.o $(OBJ) $(DEPS)
	$(CC) -o $@ $^

test-heap: test-heap.o heap.o
	$(CC) -o $@ $^

test-worst: test_worst.o $(OBJ) $(DEPS)
	$(CC) -o $@ $^

.PHONY: clean

clean:
	rm -rf *.o
	rm -rf global-heap test-heap test-worst
