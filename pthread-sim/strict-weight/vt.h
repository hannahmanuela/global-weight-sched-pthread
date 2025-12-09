#ifndef _VT_H_

#define _VT_H_

typedef long t_t;
typedef int vt_t;
typedef short w_t;
typedef unsigned long long preempt_t;
typedef unsigned short ncore_t;
typedef unsigned short cid_t;

// XXX handle vt_t wrap around
#define DUMMY (INT_MAX)   // if vt_t is DUMMY, then dummy heap_elem
#define MAXWEIGHT (SHRT_MAX)  

// [16-bit ncore, 16-bit weight, 16-bit vt, 16-bit cid]
#define NCORE(preempt) ((ncore_t) ((preempt) >> 48))
#define WEIGHT(preempt) ((w_t) ((preempt) >> 32))
#define CORE(preempt) ((cid_t) ((preempt) & ~(1 << 16)))
#define PREEMPT(ncore, w, cid) ((((preempt_t) ncore) << 48) | (((preempt_t) w) << 32) | cid)

static vt_t __calc_delta(vt_t delta_exec, w_t standard_weight, w_t actual_weight)
{
        return delta_exec * standard_weight / actual_weight;
}

static inline vt_t calc_delta(vt_t delta, w_t weight)
{
        delta = delta / weight;
        return delta;
}

#endif
