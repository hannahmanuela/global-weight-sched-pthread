#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dllist.h"

#define _DL_MARK        1UL
#define _dl_ptr(v)      ((dlnode_t *)((uintptr_t)(v) & ~_DL_MARK))
#define _dl_marked(v)   ((uintptr_t)(v) & _DL_MARK)
#define _dl_setmark(v)  ((uintptr_t)(v) | _DL_MARK)

static inline uintptr_t _dl_load_next(dlnode_t *n)
{
    return atomic_load_explicit(&n->next, memory_order_acquire);
}

void dl_init(dllist_t *list) {
    atomic_init(&list->head.next, (uintptr_t)&list->tail);
    atomic_init(&list->head.prev, (uintptr_t)NULL);
    atomic_init(&list->tail.next, (uintptr_t)NULL);
    atomic_init(&list->tail.prev, (uintptr_t)&list->head);
}

static inline bool dl_is_head(dllist_t *list, dlnode_t *n) { return n == &list->head; }
static inline bool dl_is_tail(dllist_t *list, dlnode_t *n) { return n == &list->tail; }

/*
 * dl_next - first live node after 'node', or &list->tail.
 * Skips over logically-deleted nodes.
 */
static inline dlnode_t *dl_next(dllist_t *list, dlnode_t *node)
{
    uintptr_t v = _dl_load_next(node);
    dlnode_t *cur = _dl_ptr(v);   /* strip mark from node itself if deleted */

    while (cur != &list->tail) {
        uintptr_t cv = _dl_load_next(cur);
        if (!_dl_marked(cv))
            break;
        cur = _dl_ptr(cv);
    }
    return cur;
}

/*
 * dl_prev - first live node before 'node', or &list->head.
 * Follows prev hints backward, skipping deleted nodes.
 */
static inline dlnode_t *dl_prev(dllist_t *list, dlnode_t *node)
{
    uintptr_t v = atomic_load_explicit(&node->prev, memory_order_acquire);
    dlnode_t *cur = _dl_ptr(v);

    while (cur != &list->head) {
        if (!_dl_marked(_dl_load_next(cur)))
            break;
        uintptr_t pv = atomic_load_explicit(&cur->prev, memory_order_acquire);
        cur = _dl_ptr(pv);
    }
    return cur;
}

/*
 * dl_insert_after - insert 'node' immediately after 'pred'.
 * Returns true on success; false if pred is logically deleted.
 * Caller must retry with a fresh pred when false is returned.
 */
static inline bool dl_insert_after(dllist_t *list, dlnode_t *pred, dlnode_t *node)
{
    (void)list;
    while (true) {
        uintptr_t raw = atomic_load_explicit(&pred->next, memory_order_acquire);
        if (_dl_marked(raw))
            return false;   /* pred deleted; caller finds a new pred */

        dlnode_t *succ = _dl_ptr(raw);

        atomic_store_explicit(&node->prev, (uintptr_t)pred, memory_order_relaxed);
        atomic_store_explicit(&node->next, (uintptr_t)succ, memory_order_relaxed);

        if (atomic_compare_exchange_weak_explicit(
                &pred->next, &raw, (uintptr_t)node,
                memory_order_release, memory_order_relaxed)) {
            /* Best-effort: update succ->prev; may legitimately fail. */
            uintptr_t exp = (uintptr_t)pred;
            atomic_compare_exchange_strong_explicit(
                &succ->prev, &exp, (uintptr_t)node,
                memory_order_release, memory_order_relaxed);
            return true;
        }
        /* pred->next changed (concurrent insert or delete); retry. */
    }
}

void dl_push(dllist_t *list, dlnode_t *node)
{
    while (true) {
        /* tail->prev is a hint for the last live node. */
        uintptr_t tp = atomic_load_explicit(&list->tail.prev, memory_order_acquire);
        dlnode_t *pred = _dl_ptr(tp);

        /* Walk back if the hint node is itself deleted. */
        while (pred != &list->head && _dl_marked(_dl_load_next(pred))) {
            uintptr_t pp = atomic_load_explicit(&pred->prev, memory_order_acquire);
            pred = _dl_ptr(pp);
        }

        if (dl_insert_after(list, pred, node))
            return;
    }
}

/*
 * _dl_physical_remove - after node->next is marked, physically splice
 * node out of the list by updating its direct predecessor's next pointer.
 *
 * Also tries to advance tail->prev past deleted chains as a side effect.
 */
static inline void _dl_physical_remove(dllist_t *list, dlnode_t *node)
{
    uintptr_t node_next_raw = _dl_load_next(node);
    /* node_next_raw has the mark bit set; strip it for the target. */
    dlnode_t *succ = _dl_ptr(node_next_raw);

    /* Skip any deleted successors to find a live splice target. */
    while (succ != &list->tail && _dl_marked(_dl_load_next(succ)))
        succ = _dl_ptr(_dl_load_next(succ));

    while (true) {
        /* Start backward from node->prev hint. */
        uintptr_t pv = atomic_load_explicit(&node->prev, memory_order_acquire);
        dlnode_t *pred = _dl_ptr(pv);

        /* Walk backward past any deleted predecessors. */
        while (pred != &list->head && _dl_marked(_dl_load_next(pred))) {
            uintptr_t pp = atomic_load_explicit(&pred->prev, memory_order_acquire);
            pred = _dl_ptr(pp);
        }

        /*
         * Walk pred forward to find the node whose ->next == node.
         * Along the way, help splice out any deleted nodes we cross.
         */
        while (true) {
            uintptr_t pn = _dl_load_next(pred);

            if (_dl_marked(pn)) {
                /* pred got concurrently deleted; restart outer loop. */
                break;
            }

            dlnode_t *pn_ptr = _dl_ptr(pn);

            if (pn_ptr == node) {
                /* pred is the direct predecessor; attempt splice. */
                if (atomic_compare_exchange_strong_explicit(
                        &pred->next, &pn, (uintptr_t)succ,
                        memory_order_release, memory_order_relaxed)) {
                    /* Update succ->prev hint. */
                    uintptr_t exp = (uintptr_t)node;
                    atomic_compare_exchange_strong_explicit(
                        &succ->prev, &exp, (uintptr_t)pred,
                        memory_order_release, memory_order_relaxed);
                    return;
                }
                /* CAS failed: another thread modified pred->next; retry outer. */
                break;
            }

            if (pn_ptr == &list->tail) {
                /* Walked past end — node already physically removed. */
                return;
            }

            if (_dl_marked(_dl_load_next(pn_ptr))) {
                /*
                 * pn_ptr is a deleted node sitting between pred and node.
                 * Help splice it out so we can see node directly.
                 */
                uintptr_t pn_next = _dl_load_next(pn_ptr);
                uintptr_t exp = pn;   /* pred->next == pn_ptr, unmarked */
                atomic_compare_exchange_strong_explicit(
                    &pred->next, &exp, (uintptr_t)_dl_ptr(pn_next),
                    memory_order_release, memory_order_relaxed);
                /* Retry from same pred with updated pred->next. */
            } else {
                /*
                 * pn_ptr is a live node that's not 'node'. Either it
                 * was inserted after pred, or node was already removed
                 * and pn_ptr is now pred's true successor. Advance.
                 */
                pred = pn_ptr;
            }
        }
        /* Outer retry: re-read pred from node->prev hint. */
    }
}

/*
 * After return, no new traversal will visit node. The node struct must
 * not be freed/reused until all concurrent accesses have drained.
 */
bool dl_remove(dllist_t *list, dlnode_t *node)
{
    /* Step 1: mark node->next (linearization point). */
    uintptr_t raw;
    do {
        raw = atomic_load_explicit(&node->next, memory_order_acquire);
        if (_dl_marked(raw))
            return false;   /* already deleted by another thread */
    } while (!atomic_compare_exchange_weak_explicit(
                 &node->next, &raw, _dl_setmark(raw),
                 memory_order_acq_rel, memory_order_acquire));

    /* Step 2: physically unlink node. */
    _dl_physical_remove(list, node);
    return true;
}

bool dl_empty(dllist_t *list)
{
    return dl_next(list, &list->head) == &list->tail;
}

dlnode_t *dl_pop(dllist_t *list)
{
    while (true) {
        dlnode_t *node = dl_next(list, &list->head);
        if (node == &list->tail)
            return NULL;
        if (dl_remove(list, node))
            return node;
    }
}
