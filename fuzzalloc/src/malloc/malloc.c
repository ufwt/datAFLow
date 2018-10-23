#if !defined(__x86_64__)
#error Unsupported platform
#endif

#include <stdio.h>
#include <unistd.h>

void *malloc(size_t size) {
    void *return_addr = __builtin_return_address(0);
    fprintf(stderr, "hello, malloc from %p\n", return_addr);

    return NULL;
}

void *calloc(size_t nmemb, size_t size) {
    void *return_addr = __builtin_return_address(0);
    fprintf(stderr, "hello, calloc from %p\n", return_addr);

    return NULL;
}

void *realloc(void *ptr, size_t size) {
    void *return_addr = __builtin_return_address(0);
    fprintf(stderr, "hello, realloc from %p\n", return_addr);

    return ptr;
}

void free(void *ptr) {
    fprintf(stderr, "hello, free\n");
}
