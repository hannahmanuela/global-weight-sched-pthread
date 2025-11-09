#ifndef _HEAP_H_

#define _HEAP_H_

struct heap_elem {
	void *elem;
};

struct heap {
	int (*cmp_elem)(void *, void *);
	int heap_size;
	int heap_capacity;
	struct heap_elem **heap;
}  __attribute__((aligned(64)));


struct heap *heap_new(int cmp_elem(void *, void *));
void heap_free(struct heap *h);
void heap_elem_init(struct heap_elem *h, void *e);
void *heap_min(struct heap *h);
void heap_push(struct heap *h, struct heap_elem *e);
void *heap_remove_min(struct heap *h);
void heap_iter(struct heap *h, void iter(struct heap_elem *));

#endif
	
