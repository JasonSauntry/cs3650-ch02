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
#define DEBUG
#define ASSERT
// #define STATS
// #define MEMLOG
// #define REALLOC_LOG

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
	struct bucket* the_bucket;

	void* uninitialized; // Points to where we should create the free_box, if none is in the free list.
} bunch_header;

typedef struct used_box {
	int used;
	int _unused;
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
	int used;
	int next_valid; // 0 if next is next box in mem, otherwise check free list.
	// We know what box we're in.
	// If next == 0x1, the next box in the free list is the next array cell,
	// and it itself has never been added to the free list.
	struct free_box* next;
} free_box;

const size_t box_sizes[] = {32, 56, 96, 144, 176, 272, 528, 1040, 2064, 4112, 8224, 16432};
#define  count_sizes        (1  + 1 + 1  + 1 +  1 +  1 +  1 +   1 +   1 +   1 +   1 +    1 )

size_t box_size(int number) {
	return box_sizes[number];
}

int number_from_size(size_t size) {
	for (int num = 0; num < count_sizes; num++) {
		if (size <= box_sizes[num]) return num;
	}
	return -1; // DNE
}

const size_t bunch_pages = 12;
size_t bunch_boxes(int number) {
	return (bunch_pages * PAGE_SIZE - sizeof(bunch_header)) / box_size(number);
}

void* first_box(bunch_header* bunch) {
	void* top = bunch;
	return top + sizeof(bunch_header);
}

typedef struct bucket {
	bunch_header* first_bunch;

	// Unused. TODO possibly delete.
	used_box* last_malloc; // Invariant: is null, or points to a alloc that is used.

	int size_num;


} bucket;

void init_bucket(bucket* bucket, int size_num) {
	bucket->first_bunch = 0;
	bucket->last_malloc = 0;
	bucket->size_num = size_num;

}

typedef struct MallocArena {
	pthread_mutex_t lock;
	bucket bucket[count_sizes];
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
	pthread_mutex_lock(&master_lock);
	hm_stats stats = *(hgetstats());
	fprintf(stdout, "\n== husky malloc stats ==\n");
	fprintf(stdout, "Mapped:   %ld\n", stats.pages_mapped);
	fprintf(stdout, "Unmapped: %ld\n", stats.pages_unmapped);
	fprintf(stdout, "Allocs:   %ld\n", stats.chunks_allocated);
	fprintf(stdout, "Frees:    %ld\n", stats.chunks_freed);
	fprintf(stdout, "Freelen:  %ld\n", stats.free_length);
	pthread_mutex_unlock(&master_lock);

}

// int valid_free_list(bunch_header* bunch) {
// 	free_box* box;
// 	int count = 0;
// 	for (box = bunch->free_list_header; box; count++, box = box->next) {
// 		if (!box->next_valid) break;
// 		assert(!box->used);
// 		assert(box->next != (void*) 0xa3);
// 	}
// 	return 1;
// }

void initialize_arena(struct MallocArena * arena){
	// Called exactly once per arena, guaranteed no data race.
	for (int i = 0; i < count_sizes; i++) {
		init_bucket(&arena->bucket[i], i);
	}
	pthread_mutex_init(&arena->lock, 0);
	memset(&arena->stats, 0, sizeof(hm_stats));
}

void init_box(free_box* box) {
	box->next = 0;
	box->next_valid = 0;
	box->used = 0;
}

bunch_header* init_bunch(void* mem, bunch_header* prev, 
		MallocArena* a, bucket* the_bucket) {
	bunch_header* head = mem;
	head->next = 0;
	head->prev = prev;
	head->free_list_length = bunch_boxes(the_bucket->size_num);
	head->free_list_header = 0;
	head->arena = a;
	head->the_bucket = the_bucket;

	head->uninitialized = mem + sizeof(bunch_header);

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
	void* last_box = first_box + bunch_boxes(bunch->the_bucket->size_num) 
		* box_size(bunch->the_bucket->size_num);
	return first_box <= mem && mem <= last_box;
}

bunch_header* map_bunch(bucket* sized_bucket, MallocArena* a) {
#ifdef DEBUG
	puts("Mapping new bunch");
#endif
	int pages = bunch_pages;
	size_t memsize = pages * PAGE_SIZE;
	void* foo = mmap(0, memsize, PROT_READ | PROT_WRITE, 
				MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
	a->stats.pages_mapped += pages;
	if ((size_t) foo == -1) {
		check_rv(-1);
	}

	bunch_header* bunch = init_bunch(foo, 0, arena, sized_bucket);

	// Insert into front of linked list.
	bunch->next = sized_bucket->first_bunch;
	if (bunch->next) {
		bunch->next->prev = bunch;
	}
	sized_bucket->first_bunch = bunch;
	bunch->prev = 0;
#ifdef ASSERT
	// assert(valid_free_list(bunch));
#endif

	return bunch;
}

free_box* pop_first_free(bunch_header* bunch) {
#ifdef ASSERT
	assert(bunch);
	// assert(bunch->free_list_header);
	assert(bunch->free_list_length);
#endif
	free_box* head = bunch->free_list_header;
	bunch->free_list_length--;

	if (head) {
		bunch->free_list_header = head->next;
	} else {
		head = bunch->uninitialized;
		bunch->uninitialized += box_size(bunch->the_bucket->size_num);
		init_box(head);
	}
	return head;
#ifdef ASSERT
	assert(head);
	assert(in_bunch(bunch, head));
	// if (bunch->free_list_length) {
	// 	// assert(bunch->free_list_header);
	// 	// assert(bunch->free_list_header->next);
	// 	assert(in_bunch(bunch, bunch->free_list_header->next));
	// 	assert(!bunch->free_list_header->used);
	// }
#endif
}

bucket* get_bucket(size_t total_size, MallocArena* arena) {
	int num = number_from_size(total_size);
	if (num == -1) {
		return 0;
	} else {
		// int num = number_from_size(size);
		return arena->bucket + num;
	}
}

bunch_header* get_first_usable_bunch(bucket* the_bucket) {
	bunch_header* bunch = the_bucket->first_bunch;
	int count = 0;

	while (1) {
		count++;
		if (bunch) {
			if (bunch->free_list_length > 0) {
#ifdef DEBUG
				printf("Loop iterations:\t%d\n", count);
#endif
				return bunch;
			} else {
				bunch = bunch->next;
			}
		} else {
#ifdef DEBUG
			printf("Loop iterations:\t%d\n", count);
#endif
			return 0;
		}
	}

#ifdef DEBUG
	printf("Loop iterations:\t%d\n", count);
#endif
	return bunch;
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
		arena = &arenas[(thread_number % ARENAS)];
		if (thread_number < ARENAS) {
			initialize_arena(arena);
		}

	}

	pthread_mutex_unlock(&master_lock);
	pthread_mutex_lock(&arena->lock);

	size_t size = orig_size + sizeof(used_box);

#ifdef LOG
	printf("Malloc\t%ld\n", size);
#endif

	// TODO get the right bucket.
	bucket* sized_bucket = get_bucket(size, arena);
	if (!sized_bucket) {
		// puts("TODO need a bigger box.");
		pthread_mutex_unlock(&arena->lock);
		return lalloc(orig_size + sizeof(big_box));
	} 

	arena->stats.chunks_allocated += 1;

	// Get the first bunch with stuff in it.
	bunch_header* bunch = get_first_usable_bunch(sized_bucket);

	// If there isn't a bunch available, map one.
	if (!bunch) {
		bunch = map_bunch(sized_bucket, arena);
	}

	free_box* first = pop_first_free(bunch);

	// Allocate it.
	used_box* used = (void*) first;
	used->bunch = bunch;
	// sized_bucket->last_malloc = used;

#ifdef MEMLOG
	printf("Thread:\t%p\tALlocation:\t%p\tOffset:\t%ld\n", arena, used,  
			((void*) used - (void*) bunch));
#endif
#ifdef ASSERT
	assert(bunch->uninitialized > (void*) used);
#endif

	pthread_mutex_unlock(&arena->lock);

#ifdef STATS
	hprintstats();
#endif

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
		pthread_mutex_t cpy = farena->lock;
		int i = (long)&(cpy);
		i++;
		pthread_mutex_lock(&farena->lock);
#ifdef ASSERT
		// assert(valid_free_list(bunch));
#endif
		arena->stats.chunks_freed++;

		// Return it to the free list.
		free_box* fbox = (free_box*) ubox;
		
		fbox->next = bunch->free_list_header;
		bunch->free_list_header = fbox;
		bunch->free_list_length++;
		fbox->used = 0;

		// munmap if empty.
		if (bunch->free_list_length == bunch_boxes(bunch->the_bucket->size_num)) {
#ifdef DEBUG
			puts("Unmapping bunch");
#endif 
			// TODO fix linked list.
			if (bunch->next) {
				bunch->next->prev = bunch->prev;
			}
			if (bunch->prev) {
				bunch->prev->next = bunch->next;
			} else {
				// First
				bunch->the_bucket->first_bunch = bunch->next;
			}

			munmap(bunch, bunch_pages * PAGE_SIZE);
			farena->stats.pages_unmapped += bunch_pages;
		}
		pthread_mutex_unlock(&farena->lock);
#ifdef STATS
		hprintstats();
#endif
	}
}

// use the idea from https://codereview.stackexchange.com/questions/151019/implementing-realloc-in-c
void*
xrealloc(void* prev, size_t bytes)
{
#ifdef DEBUG
	puts("Realloc");
#endif

	if (prev) {
		if (bytes == 0) {
			xfree(prev);
			return 0x0;
		} else {
			used_box* old_box = box_header(prev);
			bunch_header* old_bunch = old_box->bunch;
			int old_size_num = old_bunch->the_bucket->size_num;
			int new_size_num = number_from_size(bytes + sizeof(used_box));

			if (new_size_num <= old_size_num) {
				return prev;
			} else {
				// TODO in some cases, this can be made faster.
				MallocArena* the_arena = old_bunch->arena;
				pthread_mutex_lock(&the_arena->lock);
				int bunch_max_things = bunch_boxes(old_size_num);
				int things_in_bunch = bunch_max_things - old_bunch->free_list_length;
				// If there is only one thing, and we are first.
				if (things_in_bunch == 1 && !old_bunch->free_list_header && new_size_num < count_sizes) {
					// We can make the bunch a bigger bunch.
					// old_bunch->arena; unchanged.
#ifdef ASSERT
					assert(!old_bunch->free_list_header);
#endif 
					old_bunch->free_list_length = bunch_boxes(new_size_num) - 1;
					old_bunch->uninitialized = first_box(old_bunch) + box_size(new_size_num);

					// Fix list.
					// Remove from old list.
					if (old_bunch->next) {
						old_bunch->next->prev = old_bunch->prev;
					}
					if (old_bunch->prev) {
						old_bunch->prev->next = old_bunch->next;
					} else {
						old_bunch->the_bucket->first_bunch = old_bunch->next;
					}

					// Insert into new list.
					bucket* new_bucket = the_arena->bucket + new_size_num;

					old_bunch->the_bucket = new_bucket;
					old_bunch->next = new_bucket->first_bunch;
					if (old_bunch->next) {
						old_bunch->next->prev = old_bunch;
					}
					new_bucket->first_bunch = old_bunch;
					old_bunch->prev = 0;

					pthread_mutex_unlock(&the_arena->lock);

#ifdef ASSERT
					assert(old_box == (void*) old_bunch + sizeof(bunch_header));
#endif

					return prev;
				} else {
					pthread_mutex_unlock(&the_arena->lock);
					void* new = xmalloc(box_sizes[new_size_num] - sizeof(used_box));
					size_t old_size_raw = box_sizes[old_size_num] - sizeof(used_box);
					memcpy(new, prev, old_size_raw);
					xfree(prev);
					return new;
				}
			}
		}
	} else {
		return xmalloc(bytes);
	}


#ifdef DEBUG
	puts("Done realloc");
#endif

}
