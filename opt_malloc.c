#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include <pthread.h>
#include "xmalloc.h"
#include <string.h>
#include <assert.h>
#define NUMBER 6

// TODO: This file should be replaced by another allocator implementation.
//
// If you have a working allocator from the previous HW, use that.
//
// If your previous homework doesn't work, you can use the provided allocator
// taken from the xv6 operating system. It's in xv6_malloc.c
//
// Either way:
//  - Replace xmalloc and xfree below with the working allocator you selected.
//  - Modify the allocator as nessiary to make it thread safe by adding exactly
//    one mutex to protect the free list. This has already been done for the
//    provided xv6 allocator.
//  - Implement the "realloc" function for this allocator.

static pthread_mutex_t locks[NUMBER]={PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER};
//static header * allocated_list[NUMBER]={NULL,NULL,NULL,NULL,NULL,NULL};
//static node * free_list[NUMBER]={NULL,NULL,NULL,NULL,NULL,NULL};
int arena_selector(){
    pid_t pid= pthread_self();
    for
}


void
check_rv(int rv)
{
    if (rv == -1) {
        perror("oops");
        fflush(stdout);
        fflush(stderr);
        abort();
    }
}



const size_t PAGE_SIZE = 4096;
static hm_stats stats; // This initializes the stats to 0.
node * head_node = 0;

void coalesce(){
    if(head_node==0){
        return;
    }else{
        node * current = head_node;
//        node * previous = 0;
        while(current&&current->next){
            if(((char *)current+current->size)==((char *)current->next)){
                // increment the size
                current->size+=current->next->size;
                // delete next
                current->next=current->next->next;
                //puts("collapse one memory");
//                if(current->size>=PAGE_SIZE){
//                    node * next = current->next;
//                    int rv= munmap(current, current->size);
//                    stats.pages_unmapped+=1;
//                    check_rv(rv);
//                    //puts("munmap executed.");
//                    if(previous==0){
//                        head_node=0;
//                        return;
//                    }else {
//                        previous->next = next;
//                    }
//                }
                // restart the loop to combine more free spaces
                current=head_node;
                continue;
            }
//            previous=current;
            current=current->next;
        }
    }
}
long
free_list_length()
{
    long ans = 0;
    // TODO: Calculate the length of the free list.
    node * next = head_node;
    while(next){
        ++ans;
        next=next->next;
    }
    //printf("free list length is %ld\n", ans);
    return ans;

}

hm_stats*
hgetstats()
{
    stats.free_length = free_list_length();
    return &stats;
}

void
hprintstats()
{
    stats.free_length = free_list_length();
    fprintf(stderr, "\n== husky malloc stats ==\n");
    fprintf(stderr, "Mapped:   %ld\n", stats.pages_mapped);
    fprintf(stderr, "Unmapped: %ld\n", stats.pages_unmapped);
    fprintf(stderr, "Allocs:   %ld\n", stats.chunks_allocated);
    fprintf(stderr, "Frees:    %ld\n", stats.chunks_freed);
    fprintf(stderr, "Freelen:  %ld\n", stats.free_length);

//    node * current=head_node;
//    printf("current: %p, current->size: %zu, addition: %p, current->next: %p, subtraction: %ld, sizeof(size_t): %ld,sizeof(header): %ld, sizeof(node): %ld\n",
//           current, current->size, current+current->size, current->next, current->next-current-current->size, sizeof(size_t),sizeof(header), sizeof(node));
//    puts("\\<><><><><><><><><><><><><><><>/");
}

static
size_t
div_up(size_t xx, size_t yy)
{
    // This is useful to calculate # of pages
    // for large allocations.
    size_t zz = xx / yy;

    if (zz * yy == xx) {
        return zz;
    }
    else {
        return zz + 1;
    }
}

void*
xmalloc(size_t size)
{
    int pid = arena_selector();

    stats.chunks_allocated += 1;
    size += sizeof(header);

    if(size<sizeof(node)){
        size=sizeof(node);
    }

    pthread_mutex_lock(&locks[pid]);


    if(size>PAGE_SIZE) {
        size_t number_of_pages = div_up(size, PAGE_SIZE);
        size_t total_size_of_memory = number_of_pages * PAGE_SIZE;
        void *pointer = mmap(0, total_size_of_memory, PROT_WRITE | PROT_READ | PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        check_rv(*((int *) pointer));

        stats.pages_mapped += number_of_pages;
        // write header
//        header p1 = {total_size_of_memory}; // need to check later
//        *((header *) pointer) = p1;
        header * tmp = ((header *) pointer);
        tmp->size=total_size_of_memory;
        pthread_mutex_unlock(&locks[pid]);

        //hprintstats();
        return (void *)((char *)pointer+sizeof(header));
    }else {
        // get idea from https://www.geeksforgeeks.org/insertion-sort-for-singly-linked-list/
        node *current = head_node;
        node *previous = 0;
        while(current&& current->size<size){
            previous=current;
            current=current->next;
        }
        // if not enough space, map a new page.
        if(!current){
            void *pointer = mmap(0, PAGE_SIZE, PROT_WRITE | PROT_READ | PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
            check_rv(*((int *) pointer));

            stats.pages_mapped=stats.pages_mapped+1;
            node * new_node =((node *)pointer);
            new_node->size=PAGE_SIZE;
            // reverse order linked list insert, not sorted.
            // insert the next
            new_node->next=head_node;

            // insert the previous
            head_node = new_node;

            //reset previous and current
            current=head_node;
            previous=0;
        }
        // case 1: if the leftover is big enough to store a free list cell, return the extra to the free list.
        // code to deal with the new free list node, to substitute the old current node.
        if(current->size>size+sizeof(header)){
            node * new_free_list_node = (node *)((char *)current+size); //check
            size_t free_list_node_size = current->size-size;

            // substitute the new free list node with current node
            new_free_list_node->next=current->next;
            new_free_list_node->size=free_list_node_size;

            if(previous){
                previous->next= new_free_list_node;
            }else{
                // if previous is null, then the current node is the header
                head_node = new_free_list_node;
            }
        }
            // case 2: if the leftover is not big enough to store a free list cell, then don't create a new node.
        else{
            // delete the current node. the current node will be allocate node.
            if(previous){
                previous->next=current->next;
            }else{
                head_node=current->next;
            }
            // fill in all the size, since no extra space left.
            size=current->size;
        }
        // allocated node.
        ((header*)((void*)current))->size=size;
        //printf("free_list_node_size is: %\n" ,((header*)current)->size);
        //hprintstats();
        pthread_mutex_unlock(&locks[pid]);

        return (void*)((char *)current+sizeof(header));

        // TODO: Actually allocate memory with mmap and a free list.
    }
}

void
xfree(void* item)
{
    int pid = arena_selector();
    pthread_mutex_lock(&locks[pid]);

    stats.chunks_freed += 1;
    void * header_pointer =(void *)((char *)item - sizeof(header));
    header * tmp = (header *)(header_pointer);
//    header * tmp =  (header *)((void *)item - sizeof(header));
    size_t header_size = tmp->size;
    //printf("header_size is: %zu\n" ,header_size);
    if(header_size>=PAGE_SIZE){
        int rv = munmap(header_pointer, header_size);
        check_rv(rv);
        stats.pages_unmapped+=div_up(header_size, PAGE_SIZE);
    }
    else{
        node * current = (node *)header_pointer;


        node* previous=0;
        node* next= head_node;
        // loop to the next and current adjacent
        while(next&&(next<current)){
            previous=next;
            next=next->next;
        }
        // did not go through the loop case
        if(!previous){
            head_node=current;
        }
            // go through the loop case
        else{
            // prepare to insert current
            previous->next=current;
        }
        current->next=next;
        current->size=header_size;

    }
    // now we need to do possible coalesce
    // TODO: Actually free the item.
    coalesce();
    pthread_mutex_unlock(&locks[pid]);
    //hprintstats();
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
