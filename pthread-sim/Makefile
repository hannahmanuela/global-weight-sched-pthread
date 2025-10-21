all:
	gcc -o strict-weight/distr-weight strict-weight/distr-weight.c -pthread
	gcc -o strict-weight/global-accounting strict-weight/global-accounting.c -pthread
	gcc -o strict-weight/global-accounting-cas strict-weight/global-accounting-cas.c -pthread
	g++ -std=c++17 -pthread -o real real.cpp

cas:
	gcc -g -o strict-weight/global-accounting-cas strict-weight/global-accounting-cas.c -pthread

global-heap: strict-weight/global-heap.c strict-weight/heap.c strict-weight/group_list.c
	gcc -O2 -g -o strict-weight/global-heap strict-weight/global-heap.c strict-weight/group_list.c strict-weight/heap.c strict-weight/util.c -pthread 

test-heap: test-heap.c strict-weight/heap.c
	gcc -g -o test-heap test-heap.c strict-weight/heap.c -pthread

clean:
	rm strict-weight/global-heap-debug
	rm strict-weight/global-heap
	rm strict-weight/test-heap
	rm strict-weight/global-account-cas
