cmake_minimum_required(VERSION 3.28)
project(rbtree C)

set(CMAKE_C_STANDARD 11)

include_directories(include)

add_library(rbtree SHARED lib/rbtree.c)

add_executable(rbtree_sample samples/rbtree_sample.c)
target_link_libraries(rbtree_sample rbtree)
