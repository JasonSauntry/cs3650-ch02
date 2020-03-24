#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include <pthread.h>
#include "xmalloc.h"
#include <string.h>
#include <assert.h>
#include <stdatomic.h>
#define NUMBER 6

// #define LOG

// TODO: This file should be replaced by another allocator implementation.
//
// If you have a working allocator from the previous HW, use that.
//
// If your previous homework doesn't work, you can use the provided allocator
// taken from the xv6 operating system. It's in xv6_malloc.c
//
// Either way:
//	- Replace xmalloc and xfree below with the working allocator you selected.
//	- Modify the allocator as nessiary to make it thread safe by adding exactly
//	  one mutex to protect the free list. This has already been done for the
//	  provided xv6 allocator.
//	- Implement the "realloc" function for this allocator.
// idea from http://www.ccs.neu.edu/home/kapil/courses/cs5600f17/hw3.html

const size_t PAGE_SIZE = 4096;

/** A region of memory. Entirely sefl-sufficient, with it's own unsorted free
 * list. Keep a count of the free list length, which tells us when we can
 * munmap.
 */
typedef struct bunch_header {
	struct bunch_header* next;
	struct bunch_header* prev;
	struct free_box* free_list_header;
	long free_list_length;
} bunch_header;

typedef struct used_box {
	bunch_header* bunch;
	// available memory;
} used_box;

typedef struct big_box {
	size_t total_len;
	used_box box;
} big_box;

big_box* big_from_box(used_box* box) {
	void* mem = box;
	return mem - sizeof(size_t);
}

void* box_usable_mem(used_box* box, bunch_header* head) {
	void* pointer = (void*) box;
	box->bunch = head;
	return pointer + sizeof(used_box);
}

used_box* box_header(void* useful_mem) {
	return useful_mem - sizeof(used_box);
}

typedef struct free_box {
	// We know what box we're in.
	struct free_box* next;
} free_box;

const size_t box_size = 64;
const size_t bunch_pages = 12;
size_t bunch_boxes() {
	return (bunch_pages * PAGE_SIZE - sizeof(bunch_header)) / box_size;
}

void* first_box(bunch_header* bunch) {
	void* top = bunch;
	return top + sizeof(bunch_header);
}

// For now, only 1 bucket, which 
typedef struct bucket {
	bunch_header* first_bunch;
} bucket;

void init_bucket(bucket* bucket) {
	bucket->first_bunch = 0;
}

typedef struct MallocArena {
	pthread_mutex_t lock;
	bucket bucket; // Only 1 for now.
	hm_stats stats; 
} MallocArena;

#define ARENAS 8

MallocArena arenas[ARENAS];

__thread MallocArena* arena = 0;
static atomic_int last_arena_num = 0;

int 
check_rv(int rv)
{
	if (rv <= -1) {
		perror("oops");
		fflush(stdout);
		fflush(stderr);
		abort();
	} else {
		  return rv;
	}
}

hm_stats*
hgetstats()
{
	// TODO implement free_length.
	arena->stats.free_length = -1;
	return &arena->stats;
}

void
hprintstats()
{
	hm_stats stats = *(hgetstats());
	fprintf(stderr, "\n== husky malloc stats ==\n");
	fprintf(stderr, "Mapped:   %ld\n", stats.pages_mapped);
	fprintf(stderr, "Unmapped: %ld\n", stats.pages_unmapped);
	fprintf(stderr, "Allocs:   %ld\n", stats.chunks_allocated);
	fprintf(stderr, "Frees:    %ld\n", stats.chunks_freed);
	fprintf(stderr, "Freelen:  %ld\n", stats.free_length);

}

// static
// size_t
// div_up(size_t xx, size_t yy)
// {
// 	// This is useful to calculate # of pages
// 	// for large allocations.
// 	size_t zz = xx / yy;
// 
// 	if (zz * yy == xx) {
// 		return zz;
// 	}
// 	else {
// 		return zz + 1;
// 	}
// }

void initialize_arena(struct MallocArena * arena){
	// Called exactly once per arena, guaranteed no data race.
	init_bucket(&arena->bucket);
	pthread_mutex_init ( &arena->lock, NULL);
	memset(&arena->stats, 0, sizeof(hm_stats));
}

bunch_header* init_bunch(void* mem, bunch_header* prev) {
	bunch_header* head = mem;
	head->next = 0;
	head->prev = prev;
	head->free_list_length = bunch_boxes();
	head->free_list_header = first_box(head);
	return head;
}

void* lalloc(size_t size) {
	big_box* big = mmap(0, size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
	big->box.bunch = 0;
	big->total_len = size;
	return (void*) big + sizeof(big_box);
}

void lfree(void* item, size_t size) {
	check_rv(munmap(item, size));
}

void*
xmalloc(size_t orig_size)
{
	if(arena==NULL){
		int thread_number;
		thread_number = atomic_fetch_add(&last_arena_num, 1); 
		arena = arenas + (thread_number % ARENAS);
		if (thread_number < ARENAS) {
			initialize_arena(arena);
		}
	}

	arena->stats.chunks_allocated += 1;
	size_t size = orig_size + sizeof(used_box);

#ifdef LOG
	printf("Malloc\t%ld\n", size);
#endif


	// TODO get the right bucket.
	bucket* sized_bucket;
	if (size > box_size) {
		// puts("TODO need a bigger box.");
		// abort();
		// TODO uh, duh.
		return lalloc(orig_size + sizeof(big_box));
	} else {
		sized_bucket = &arena->bucket;
		// puts("Box fits");
	}

	// Get the first bunch with stuff in it.
	bunch_header* bunch = sized_bucket->first_bunch;
	bunch_header* prev = 0;
	for (bunch = sized_bucket->first_bunch; bunch && bunch->free_list_length == 0; bunch = bunch->next) {
		prev = bunch;
	}

	// TODO if there isn't a bunch available, map one.
	if (!bunch) {
		int pages = bunch_pages;
		size_t memsize = pages * PAGE_SIZE;
		void* foo = mmap(0, memsize, PROT_READ | PROT_WRITE, 
					MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
		if ((size_t) foo == -1) {
			check_rv(-1);
		}
		bunch = init_bunch(foo, prev);

	}
	
	// Select the first free box in the bunch.
	free_box* first = bunch->free_list_header;
	bunch->free_list_header = first->next;

	// Allocate it.
	used_box* used = (void*) first;
	used->bunch = bunch;
	bunch->free_list_length--;

	// Done
	return box_usable_mem(used, bunch);
}

void
xfree(void* item)
{
	used_box* ubox = box_header(item);
	if (ubox->bunch == 0) {
		// Large allocation;
		// TODO this also is temporary.
		big_box* big = big_from_box(ubox);
		lfree(big, big->total_len);
	} else {
		// Return it to the free list.
		bunch_header* bunch = ubox->bunch;
		free_box* fbox = (free_box*) ubox;
		
		fbox->next = bunch->free_list_header;
		bunch->free_list_header = fbox;
		bunch->free_list_length++;

		// TODO munmap if empty.
		if (bunch->free_list_length == bunch_boxes()) {
			munmap(bunch, bunch_pages * PAGE_SIZE);
		}
	}
}

// use the idea from https://codereview.stackexchange.com/questions/151019/implementing-realloc-in-c
void*
xrealloc(void* prev, size_t bytes)
{

	header * allocated_node = (header *)((char*)prev-sizeof(header));
	size_t old_size =  allocated_node->size-sizeof(header);
	// first, check if the new length is 0,
	// if it is, free old pointer and return null.
	if(bytes==0){
		xfree(prev);
		return NULL;
	}
		// second, check if the old pointer is null or not.
		// if it is, return allocated memory address.
	else if(!prev){

		return xmalloc(bytes);
	}

		// third, check if new size smaller than the old size,
		// if it is, return old pointer.
	else if(bytes<=old_size){
		return prev;
	}
		// last, check if prev and lengths inequality,
		// and then allocate new space and free old space.
		// here, memcpy from string.h gives us a good option to copy bytes on the memory.
	else{
		assert((prev)&&(bytes>=old_size));
		void * ret = xmalloc(bytes);
		// check if malloc returns a good pointer.
		if(ret){
			memcpy(ret, prev, old_size);
			xfree(prev);
		}
		// return new pointer.
		return ret;
	}
}
