# Optimized Allocator Report

## Against Hw 8

+-------+-------------+------------+-------------+------------+
| Input | Ivec - Hw 8 | Ivec - Opt | List - Hw 8 | List - Opt |
+=======|=============|============|=============|============+
| 1000  | 104 ms      | 13 ms      | 4.093 ms    | 49 ms      |
| 10000 | 21939 ms    | 153 ms     | --          | 737 ms     |
+-------|-------------|------------|-------------|------------+

The optimized allocator was significanlty faster than the Hw 8
allocator, by at least an order of magnitude. In most large cases, the
optimized allocator was around 100 times faster than the Hw 8
allocater. As is shown in the next section, the optimized allocator
isn't all that optimized. The fact that is is *still* that much better
than the Hw 8 allocator really shows the inefficiency of the simple
free-list design, with linear-time malloc and free.

## Against System Malloc and Free

+--------|------------|------------|------------|------------+
| Input  | Ivec - Sys | Ivec - Opt | List - Sys | List - Opt |
+========|============|============|============|============+
| 100000 | 1.433 s    | 1.944 s    | 3.161 s    | 10.289 s   |
| 500000 | 8.334 s    | 11.153 s   | 18.988 s   | --         |
+--------|------------|------------|------------|------------+

THe optimized allocator was *almost* as fast as the system allocator.
In the list cases, the optimized allocator generally took 2 - 4 times
as long as the system allocator. In the vector case, it took 20 - 50%
longer. This is disappointingly short of the goal of beating the
system allocator, but it is somewhat close.

## Optimized Design

The basic design of the allocator was a bucket-based allocator with
multiple arenas. Within each bucket was a linked list of "bunches", a
block of memory that was `mmap`ed together and will eventually be
`munmap`ed together. Each bunch stores its state internally, and is
conceptually its own allocator, separate from all other bunches and
buckets. When malloc is called, the allocator chooses the
correct-sized bucket, then finds a bunch with space (usually the first
bunch on the list), then that bunch finds space for the allocation.

### Free List

Each bucket has a list of bunches it can allocate from. Usually, when
the last space in a bunch is allocated, it is removed from this list,
and reinserted when something is freed. Thus, finding a bunch with
space is usually constant-time. If there is no bunch, a new one will
be mapped.

Each bunch maintains its own free list of space to allocate. This list
is unsorted, allowing for constant-time push and pop. However, it
cannot be initialized in constant time. To solve this, the free list
only contains memory that has *already been allocated, and then been
freed*. Memory that has never been allocated is referenced by a single
pointer in each bunch. Since the free list is used if it is not empty,
this array of never-allocated space is never fragmented. This allows
us to take advantage of `mmap` being lazy. We can `mmap` a large block
of pages to a bunch, knowing that if some of these pages are never
used, they will never actually be mapped to physical memory.

### Thread-local Arenas

This allocator handles parallelization using multiple arenas. Each
thread is assigned an arena, and all of its malloc calls will he
handled by that arena. There are a fixed number of arenas. If there
are more threads than arenas, two threads will share an arena. This
was not the case in the test programs, which had 5 threads for 8
arenas.

Free calls are handled by whatever arena made the original allocation.

### Bunch Re-assignment

It is a somewhat common case that `realloc` is called when the bunch
containing the old allocation has one 1 allocation in it. In one test,
this accounted for 25% of all calls to `realloc`. In this case, the
bunch will re-assign itself to a new bucket, resizing all memory
allocations within itself. Since there is only one, and it called
`realloc` in the first place, this is safe.

### Memory Re-use

Each bunch keeps a count of the number of free spaces it has. Since it
knows how many total spaces it has, it can detect when it is empty.
When a bunch is empty, it `munmap`s itself, during the `free` call
that empties it.

## Challenge

The most significant challenge in building this allocator was one bug
that took 2 days to fix. It ended up being a single line of bad
pointer arithmetic, which lead `realloc` incorrectly calculating the
size of the existing allocation, which resulted in not always
re-allocating when it was warranted. It was a very simple problem, but
the way it occurred caused errors to appear far away from the
incorrect line. It was very difficult to debug.

## Reflection on Design

If we were to redo this assignment, we would likely use the same
general allocator design. Buckets are fast, with constant-time
`malloc` and `free`, and are simpler and more flexible than a buddy
system. Although out optimized allocator *did* fall short of its goal,
we feel that this is not a product of an inherit flaw. Rather, this is
simply a result of insufficient time to further optimize the
allocator. We feel that if we had more time, we *could* improve our
allocator to the point that it would beat the system allocator.
