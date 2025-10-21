all:
	gcc -o strict-weight/distr-weight strict-weight/distr-weight.c -pthread
	gcc -o strict-weight/global-accounting strict-weight/global-accounting.c -pthread
	gcc -o strict-weight/global-accounting-cas strict-weight/global-accounting-cas.c -pthread
	g++ -std=c++17 -pthread -o real real.cpp

cas:
	gcc -g -o strict-weight/global-accounting-cas strict-weight/global-accounting-cas.c -pthread

global-heap:
	gcc -O2 -o strict-weight/global-heap strict-weight/global-heap.c strict-weight/heap.c -pthread

global-heap-debug:
	gcc -g -pg -o strict-weight/global-heap strict-weight/global-accounting-rlock.c -pthread

test-heap:
	gcc -g -o test-heap test-heap.c strict-weight/heap.c -pthread

clean:
	rm strict-weight/distr-weight
	rm strict-weight/global-accounting
	rm strict-weight/global-accounting-cas
