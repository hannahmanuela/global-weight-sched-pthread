#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

void error(char *s) {
	fprintf(stderr, "error: %s\n", s);
	exit(1);
}

double
now()
{
 struct timeval tv;
 gettimeofday(&tv, 0);
 return tv.tv_sec + tv.tv_usec / 1000000.0;
}
