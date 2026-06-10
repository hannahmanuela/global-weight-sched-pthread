OBJ = sched_state.o gwfs.o rr.o pcrq.o gq.o process.o group.o mheap.o heap.o util.o ticks.o core.o lock.o mcounter.o scheduler.o running.o preempt.o dllist.o

CC=gcc

CFLAGS += -I. -MMD -MP
#CFLAGS += -g
#CFLAGS += -O2
# CFLAGS += -fsanitize=thread
CFLAGS += -g -O2 -fno-omit-frame-pointer # -march=emeraldrapids
DEPS = $(wildcard *.d)
-include $(DEPS)

all: schedule test-heap test-gwfs test-mcntr test-preempt test-dllist test-gq test-mheap logmerge logdump rankerror

schedule: driver.o $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

test-gwfs: test_gwfs.o $(OBJ)
	$(CC) -o $@ $^

test-heap: test_heap.o heap.o lock.o core.o
	$(CC) -o $@ $^

test-mcntr: test_mcntr.o mcounter.o lock.o core.o util.o
	$(CC) -o $@ $^

test-preempt: test_preempt.o preempt.o lock.o core.o util.o
	$(CC) -o $@ $^

test-running: test_running.o running.o process.o mheap.o heap.o ticks.o lock.o core.o util.o
	$(CC) -o $@ $^

test-dllist: test_dllist.o dllist.o lock.o core.o util.o
	$(CC) -o $@ $^

test-gq: test_gq.o  lock.o core.o util.o
	$(CC) -o $@ $^

test-mheap: test_mheap.o process.o mheap.o heap.o ticks.o core.o util.o lock.o
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
	rm -rf schedule test-heap test-global
