#include "vt.h"

// delta_exec * standard_weight / actual_weight
static vt_t __calc_delta(vt_t delta_exec, int standard_weight, int actual_weight)
{
        return delta_exec * standard_weight / actual_weight;
}

static inline vt_t calc_delta(vt_t delta, int weight)
{
        // if (weight != EXP_WEIGHT)
        //      delta = __calc_delta(delta, EXP_WEIGHT, weight);
        printf("delta: %d, weight: %d, %d\n", delta, weight, delta/weight);
        delta = delta / weight;
        return delta;
}
