#include "mheap.h"
#include "group.h"

struct group_list {
	struct mheap *mheap;
} __attribute__((aligned(64)));

struct group_list *gl_new(int nqueue);
void gl_print(struct group_list *gl);
void gl_stats(struct group_list *gl);
struct group *gl_min_group(struct group_list *);
int gl_avg_spec_virt_time(struct group *group_to_ignore);
int gl_avg_spec_virt_time_inc(struct group *group_to_ignore);
void gl_register_group(struct group_list *, struct group *g);
void gl_unregister_group(struct group_list *, struct group *g);



