CC=gcc
# CFLAGS=-I. -g
CFLAGS=-I. -O2
# CFLAGS=-I. -O2 -g -pg
DEPS = global_heap.h group.h heap.h heap_elem.h lheap.h mheap.h util.h  ticks.h core.h vt.h
OBJ = global_heap.o group.o mheap.o lheap.o heap.o util.c ticks.o core.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $(CFLAGS) $<

all: global-heap test-heap test-global

global-heap: driver.o $(OBJ) $(DEPS)
	$(CC) -o $@ $^

test-heap: test_heap.o heap.o
	$(CC) -o $@ $^

test-global: test_global.o $(OBJ) $(DEPS)
	$(CC) -o $@ $^

.PHONY: clean

clean:
	rm -rf *.o
	rm -rf global-heap test-heap test-global
