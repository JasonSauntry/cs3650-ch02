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
// #define DEBUG
// #define MEMLOG
#define ASSERT

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
	struct MallocArena* arena;
} bunch_header;

typedef struct used_box {
	long used;
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
	box->used = 1;
	return pointer + sizeof(used_box);
}

used_box* box_header(void* useful_mem) {
	return useful_mem - sizeof(used_box);
}

typedef struct free_box {
	long used;
	// We know what box we're in.
	// If next == 0x1, the next box in the free list is the next array cell,
	// and it itself has never been added to the free list.
	struct free_box* next;
} free_box;

const size_t box_size = 80;
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
	used_box* last_malloc; // Invariant: is null, or points to a alloc that is used.
} bucket;

void init_bucket(bucket* bucket) {
	bucket->first_bunch = 0;
	bucket->last_malloc = 0;
}

typedef struct MallocArena {
	pthread_mutex_t lock;
	bucket bucket; // Only 1 for now.
	hm_stats stats; 
} MallocArena;

pthread_mutex_t master_lock;
int master_init = 0;

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

void initialize_arena(struct MallocArena * arena){
	// Called exactly once per arena, guaranteed no data race.
	init_bucket(&arena->bucket);
	pthread_mutex_init (&arena->lock, 0);
	memset(&arena->stats, 0, sizeof(hm_stats));
}

bunch_header* init_bunch(void* mem, bunch_header* prev, MallocArena* a) {
	bunch_header* head = mem;
	head->next = 0;
	head->prev = prev;
	head->free_list_length = bunch_boxes();
	head->free_list_header = first_box(head);
	head->free_list_header->next = (void*) 0x1; // Next block in array, not in free list yet.
	head->arena = a;
	return head;
}

void* lalloc(size_t size) {
#ifdef LOG
	printf("Lalloc\t%ld\n", size);
#endif
	big_box* big = mmap(0, size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
	big->box.bunch = 0;
	big->total_len = size;
	return (void*) big + sizeof(big_box);
}

void lfree(void* item, size_t size) {
#ifdef LOG
	puts("Lfree");
#endif
	check_rv(munmap(item, size));
}

int in_bunch(bunch_header* bunch, void* mem) {
	void* start = bunch;
	void* first_box = start + sizeof(bunch_header);
	void* last_box = first_box + bunch_boxes() * box_size;
	return first_box <= mem && mem <= last_box;
}

void*
xmalloc(size_t orig_size)
{
	if (!master_init++) {
		pthread_mutex_init(&master_lock, 0);
	}
	pthread_mutex_lock(&master_lock);
	if(arena==NULL){
		int thread_number;
		thread_number = atomic_fetch_add(&last_arena_num, 1); 
		arena = arenas + (thread_number % ARENAS);
		if (thread_number < ARENAS) {
			initialize_arena(arena);
		}
	}

	pthread_mutex_lock(&arena->lock);

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
		pthread_mutex_unlock(&arena->lock);
		pthread_mutex_unlock(&master_lock);
		return lalloc(orig_size + sizeof(big_box));
	} else {
		sized_bucket = &arena->bucket;
		// puts("Box fits");
	}

	free_box* first;
	bunch_header* bunch;

	used_box* last = sized_bucket->last_malloc;
	free_box* last_next = (void*) last + box_size;
	if (0 && last && in_bunch(last->bunch, last + box_size) && !last_next->used) {
		first = last_next;
		bunch = last->bunch;
		// TODO fix my free list.
	} else {
		

		// Get the first bunch with stuff in it.
		bunch = sized_bucket->first_bunch;
		bunch_header* prev = 0;
		int count = 0;

		for (bunch = sized_bucket->first_bunch; bunch && bunch->free_list_length == 0; bunch = bunch->next) {
			prev = bunch;
			count++;
		}

#ifdef DEBUG
		printf("Loop iterations:\t%d\n", count);
#endif	

		// If there isn't a bunch available, map one.
		if (!bunch) {
#ifdef DEBUG
			puts("Mapping new bunch");
#endif
			int pages = bunch_pages;
			size_t memsize = pages * PAGE_SIZE;
			void* foo = mmap(0, memsize, PROT_READ | PROT_WRITE, 
						MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
			if ((size_t) foo == -1) {
				check_rv(-1);
			}

			bunch = init_bunch(foo, prev, arena);

			// Insert into front of linked list.
			bunch->next = sized_bucket->first_bunch;
			if (bunch->next) {
				bunch->next->prev = bunch;
			}
			sized_bucket->first_bunch = bunch;
			bunch->prev = 0;

		}

		// Select the first free box in the bunch.
		first = bunch->free_list_header;
		if (first->next == (void*) 0x1) {
#ifdef DEBUG
			puts("first->next == 0x1");
#endif
			if (bunch->free_list_length > 1) {
				bunch->free_list_header = (void*) first + box_size;
				// Next is garbage, important bits should be initialized.
				bunch->free_list_header->next = (void*) 0x1;
				bunch->free_list_header->used = 0;
			} else {
				bunch->free_list_header = 0;
			}
		} else {
			bunch->free_list_header = first->next;
		}
#ifdef ASSERT
		if (first->used) {
			puts("Bad free list");
			abort();
		}
		// printf("%d\n", bunch->free_list_header && bunch->free_list_header->used);
		if(bunch->free_list_length > 1 && bunch->free_list_header->used) {
			puts("foo");
			int used = bunch->free_list_header->used;
			if (used) {
				puts("Bad");
				abort();
			}
		}
		assert(bunch->next != (void*) 0x1);
		if (bunch->prev == (void*) 0x1) {
			puts("Bad");
			abort();
		}
#endif
		
	}

	// Allocate it.
	used_box* used = (void*) first;
	used->bunch = bunch;
	bunch->free_list_length--;
	sized_bucket->last_malloc = used;
	assert(bunch->free_list_header || bunch->free_list_length == 0);
	
#ifdef MEMLOG
	printf("Thread:\t%ld\tALlocation:\t%ld\n", (long) arena, (long) used);
#endif

	pthread_mutex_unlock(&arena->lock);
	pthread_mutex_unlock(&master_lock);

	// Done
	return box_usable_mem(used, bunch);
}

void
xfree(void* item)
{
#ifdef DEBUG
	puts("Free");
#endif
	used_box* ubox = box_header(item);
	if (ubox->bunch == 0) {
		// Large allocation;
		// This is freed only once, so no race.
		big_box* big = big_from_box(ubox);
		lfree(big, big->total_len);
	} else {
		// First, find the arena, and take the mutex.
		bunch_header* bunch = ubox->bunch;
		MallocArena* farena = bunch->arena;
		pthread_mutex_lock(&farena->lock);

		// Return it to the free list.
		free_box* fbox = (free_box*) ubox;
		
		fbox->next = bunch->free_list_header;
		bunch->free_list_header = fbox;
		bunch->free_list_length++;
		fbox->used = 0;
		if (arena->bucket.last_malloc == (void*) fbox) {
			arena->bucket.last_malloc = 0;
		}

		// munmap if empty.
		if (bunch->free_list_length == bunch_boxes()) {
#ifdef DEBUG
			puts("Unmapping bunch");
#endif 
			// TODO fix linked list.
			// munmap(bunch, bunch_pages * PAGE_SIZE);
		}
		pthread_mutex_unlock(&farena->lock);
	}
}

// use the idea from https://codereview.stackexchange.com/questions/151019/implementing-realloc-in-c
void*
xrealloc(void* prev, size_t bytes)
{
#ifdef DEBUG
	puts("Realloc");
#endif

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
#ifdef DEBUG
		puts("Done realloc");
#endif
		return ret;
	}

#ifdef DEBUG
	puts("Done realloc");
#endif

}
