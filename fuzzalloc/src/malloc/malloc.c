#include <stdio.h>

__attribute__((constructor))
static void __init_memory_allocator(void) {
    printf("hello, constructor\n");
}

void *malloc(size_t size) {
    return NULL;
}

void *calloc(size_t nmemb, size_t size) {
    return NULL;
}

void *realloc(void *ptr, size_t size) {
    return ptr;
}

void free(void *ptr) {
}
