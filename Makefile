all:
	gcc -o main main.c -pthread

clean:
	rm main

run:
	./main 4


