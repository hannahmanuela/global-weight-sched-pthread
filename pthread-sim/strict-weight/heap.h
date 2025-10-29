// Min-heap of groups keyed by val

struct heap_elem {
	int heap_index;
	void *elem;
};

struct heap {
	int (*cmp_elem)(void *, void *);
	int heap_size;
	int heap_capacity;
	
	int min_vrt;
	int n;
	struct heap_elem **heap;

}  __attribute__((aligned(64)));


struct heap *heap_new(int cmp_elem(void *, void *));
void heap_elem_init(struct heap_elem *h, void *e);
int heap_elem_idx(struct heap_elem *h);
void *heap_min(struct heap *h);
void *heap_lookup(struct heap *heap, int idx);
void heap_ensure_capacity(struct heap *h);
void heap_push(struct heap *h, struct heap_elem *e);
void heap_remove_at(struct heap *h, struct heap_elem *e);
void heap_fix_index(struct heap *h, struct heap_elem *e);
void heap_iter(struct heap *h, void iter(struct heap_elem *));
struct heap_elem* heap_first(struct heap *);
struct heap_elem* heap_next(struct heap *h, struct heap_elem *e);
	
