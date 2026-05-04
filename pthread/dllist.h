#ifndef _DLLIST_H_

#define _DLLIST_H_

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// Lock-free doubly linked list with embedded nodes.
//
// Based on Sundell & Tsigas (2004), single-word CAS variant.
//

typedef struct dlnode dlnode_t;

struct dlnode {
    _Atomic(uintptr_t) next;   /* ptr-to-next | 1 when logically deleted */
    _Atomic(uintptr_t) prev;   /* ptr-to-prev — hint, may be stale        */
};

typedef struct {
    dlnode_t head;   /* sentinel: never deleted, no real element */
    dlnode_t tail;   /* sentinel: never deleted, no real element */
} dllist_t;

void dl_init(dllist_t *list);
bool dl_empty(dllist_t *list);
bool dl_remove(dllist_t *list, dlnode_t *node);
void dl_push(dllist_t *list, dlnode_t *node);
dlnode_t *dl_pop(dllist_t *list);

#endif
