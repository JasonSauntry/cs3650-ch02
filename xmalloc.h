#ifndef XMALLOC_H
#define XMALLOC_H

#include <stddef.h>

void* xmalloc(size_t bytes);
void  xfree(void* ptr);
void* xrealloc(void* prev, size_t bytes);



typedef struct hm_stats {
    long pages_mapped;
    long pages_unmapped;
    long chunks_allocated;
    long chunks_freed;
    long free_length;
} hm_stats;
typedef struct node_t {
    size_t size;
    struct node_t* next;
} node;
typedef struct header_t{
    size_t size;
}header;

// void check_rv(int rv);

hm_stats* hgetstats();
void hprintstats();

#endif
