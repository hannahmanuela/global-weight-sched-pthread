OBJ = global_heap.o group.o mheap.o lheap.o heap.o util.c ticks.o core.o lock.o

CC=gcc

CFLAGS += -I. -MMD -MP
#CFLAGS += -g
#CFLAGS += -O2
CFLAGS += -g -O2 -fno-omit-frame-pointer
DEPS = $(wildcard *.d)
-include $(DEPS)

all: global-heap test-heap test-global

global-heap: driver.o $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

test-global: test_global.o $(OBJ)
	$(CC) -o $@ $^

test-heap: test_heap.o heap.o
	$(CC) -o $@ $^

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean

clean:
	rm -rf *.o *.d
	rm -rf global-heap test-heap test-global
