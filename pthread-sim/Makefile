all:
	gcc -o strict-weight/distr-weight strict-weight/distr-weight.c -pthread
	gcc -o strict-weight/global-accounting strict-weight/global-accounting.c -pthread
	gcc -o strict-weight/global-accounting-cas strict-weight/global-accounting-cas.c -pthread
	g++ -std=c++17 -pthread -o real real.cpp

cas:
	gcc -g -o strict-weight/global-accounting-cas strict-weight/global-accounting-cas.c -pthread

rlock:
	gcc -O2 -o strict-weight/global-accounting-rlock strict-weight/global-accounting-rlock.c strict-weight/heap.c -pthread

rlock-debug:
	gcc -g -pg -o strict-weight/global-accounting-rlock strict-weight/global-accounting-rlock.c -pthread

test-heap:
	gcc -g -o test-heap test-heap.c -pthread

clean:
	rm strict-weight/distr-weight
	rm strict-weight/global-accounting
	rm strict-weight/global-accounting-cas
