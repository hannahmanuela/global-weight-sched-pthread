all:
	gcc -o strict-weight/distr-weight strict-weight/distr-weight.c -pthread
	gcc -o strict-weight/global-accounting strict-weight/global-accounting.c -pthread

clean:
	rm strict-weight/distr-weight
	rm strict-weight/global-accounting

