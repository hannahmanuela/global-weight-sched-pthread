all:
	gcc -o strict-weight/distr-weight strict-weight/distr-weight.c -pthread

clean:
	rm strict-weight/distr-weight


