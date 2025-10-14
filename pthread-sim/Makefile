all:
	gcc -o strict-weight/distr-weight strict-weight/distr-weight.c -pthread
	gcc -o strict-weight/global-accounting strict-weight/global-accounting.c -pthread
	gcc -o strict-weight/global-accounting-cas strict-weight/global-accounting-cas.c -pthread
	g++ -std=c++17 -pthread -o real real.cpp

cas:
	gcc -g -o strict-weight/global-accounting-cas strict-weight/global-accounting-cas.c -pthread

rlock:
	gcc -O2 -pg -o strict-weight/global-accounting-rlock strict-weight/global-accounting-rlock.c -pthread

clean:
	rm strict-weight/distr-weight
	rm strict-weight/global-accounting
	rm strict-weight/global-accounting-cas
