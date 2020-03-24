cmake_minimum_required(VERSION 3.15)
project(ch02 C)

set(CMAKE_C_STANDARD 99)

add_executable(ch02 main.c frag_main.c hwx_malloc.c ivec.h ivec_main.c list.h list_main.c opt_malloc.c sys_malloc.c xmalloc.h xv6_malloc.c)