CC=gcc
CFLAGS=-I. -O2 -g
DEPS = global_heap.h group.h group_list.h heap.h lheap.h mheap.h util.h 
OBJ = driver.o global_heap.o group_list.o group.o mheap.o lheap.o heap.o util.c

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $(CFLAGS) $<

all: global-heap test-heap

global-heap: $(OBJ) $(DEPS)
	$(CC) -o $@ $^

test-heap: test-heap.o heap.o
	$(CC) -o $@ $^

.PHONY: clean

clean:
	rm -rf *.o
	rm -rf global-heap test-heap
