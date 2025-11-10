#ifndef _VT_H_

#define _VT_H_

typedef long t_t;
typedef unsigned int vt_t;
typedef int w_t;

// XXX handle vt_t wrap around
#define DUMMY (UINT_MAX)   // if vt_t is DUMMY, then dummy heap_elem

// delta_exec * standard_weight / actual_weight
static vt_t __calc_delta(vt_t delta_exec, w_t standard_weight, w_t actual_weight)
{
        return delta_exec * standard_weight / actual_weight;
}

static inline vt_t calc_delta(vt_t delta, w_t weight)
{
        // if (weight != EXP_WEIGHT)
        //      delta = __calc_delta(delta, EXP_WEIGHT, weight);
        //printf("delta: %d, weight: %d, %d\n", delta, weight, delta/weight);
        delta = delta / weight;
        return delta;
}

#endif
