#include <stdlib.h>

#include <ticks.h>

extern int num_cores;

t_t *new_ticks() {
	return malloc(sizeof(t_t) * num_cores);
}

void ticks_free(t_t *ticks) {
	free(ticks);
}

void ticks_sub(t_t *res, t_t *sub) {
	for (int i = 0; i < num_cores; i++) {
		res[i] -= sub[i];
	}
}

void ticks_add(t_t *res, t_t *add) {
	for (int i = 0; i < num_cores; i++) {
		res[i] += add[i];
	}
}

t_t ticks_sum(t_t *ticks) {
	t_t sum = 0;
	for (int i = 0; i < num_cores; i++) {
		sum += ticks[i];
	}
	return sum;
}
