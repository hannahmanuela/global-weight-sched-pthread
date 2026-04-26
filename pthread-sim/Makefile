OBJ = global_heap.o rr.o group.o mheap.o heap.o util.c ticks.o core.o lock.o mcounter.o

CC=gcc

CFLAGS += -I. -MMD -MP
#CFLAGS += -g
#CFLAGS += -O2
CFLAGS += -g -O2 -fno-omit-frame-pointer
DEPS = $(wildcard *.d)
-include $(DEPS)

all: global-heap test-heap test-global test-mcntr logmerge logdump rankerror

global-heap: driver.o $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

test-global: test_global.o $(OBJ)
	$(CC) -o $@ $^

test-heap: test_heap.o heap.o lock.o
	$(CC) -o $@ $^

test-mcntr: test_mcntr.o mcounter.o lock.o core.o util.o
	$(CC) -o $@ $^

logmerge: logmerge.o
	$(CC) -o $@ $^

logdump: logdump.o
	$(CC) -o $@ $^

rankerror: rankerror.o
	$(CC) -o $@ $^

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean

clean:
	rm -rf *.o *.d
	rm -rf global-heap test-heap test-global
