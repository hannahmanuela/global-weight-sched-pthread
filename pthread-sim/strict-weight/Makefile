CC=gcc
CFLAGS=-I. -g -O2
DEPS = group.h group_list.h heap.h lheap.h mheap.h util.h
OBJ = global-heap.o group_list.o group.o mheap.o lheap.o heap.o util.c

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $(CFLAGS) $<

all: global-heap test-heap

global-heap: $(OBJ)
	$(CC) -o $@ $^

test-heap: test-heap.o heap.o
	$(CC) -o $@ $^

.PHONY: clean

clean:
	rm -rf *.o
	rm -rf global-heap test-heap
