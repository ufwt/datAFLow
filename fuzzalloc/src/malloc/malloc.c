#if !defined(__x86_64__)
#error Unsupported platform
#endif

#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>

#include "fuzzalloc.h"

// TODO delete
#define DEBUG 1

#ifdef DEBUG
#include <stdio.h>
#define DEBUG_MSG(format, ...)                                                 \
  fprintf(stderr, "%s(): " format, __func__, ##__VA_ARGS__)
#else
#define DEBUG_MSG(format, ...)
#endif

/// Alignment used by muslc
#define SIZE_ALIGN (4 * sizeof(size_t))

/// When a allocation site pool is initially created, make it this times as
/// many bytes larger than the original request (to handle subsequent requests
/// at the same allocation site)
#define POOL_SIZE_MULTIPLIER 4

struct chunk_t {
  size_t size;          //< LSB indicates whether in use or free
  struct chunk_t *next; //< Pointer to next free chunk
};

struct pool_t {
  uint8_t in_use : 1;
  struct {
    size_t allocated_size;
    size_t used_size;
    struct chunk_t *entry;
  };
};

/// Size of chunk overhead (in bytes)
#define CHUNK_OVERHEAD (sizeof(struct chunk_t))

/// Returns non-zero if the chunk \p c is in use
#define IN_USE(c) (c->size & ((size_t)1))

/// Set the chunk as being in use
#define SET_IN_USE(c) (c->size |= ((size_t)1))

/// Set the chunk as being free
#define CLEAR_USE(c) (c->size &= ((size_t)(~1)))

/// Chunk size (in bytes), ignoring the in use/free bit
#define CHUNK_SIZE(c) (c->size & ((size_t)(~0) - 1))

/// Set the size of the chunk
#define SET_CHUNK_SIZE(c, s)                                                   \
  (c->size = (size_t)(s - (SIZE_ALIGN - CHUNK_OVERHEAD)))

/// The maximum possible chunk size (in bytes)
#define MAX_CHUNK_SIZE ((size_t)(~0) - 1)

/// Pointer to next chunk
#define NEXT_CHUNK(c) ((struct chunk *)((uint8_t *)(c) + CHUNK_SIZE(c)))

/// Convert memory address (as seen by the user) to a chunk
#define MEM_TO_CHUNK(p) ((struct chunk *)((uint8_t *)(p)-CHUNK_OVERHEAD))

/// Convert chunk to a memory address (as seen by the user)
#define CHUNK_TO_MEM(c) ((void *)((uint8_t *)(c) + CHUNK_OVERHEAD))

// TODO more space-efficient data structure
static struct pool_t tag_map[TAG_MAX + 1];

static struct pool_t initialize_pool(size_t allocated_size,
                                     struct chunk_t *chunk) {
  struct pool_t pool;

  pool.in_use = 1;
  pool.allocated_size = allocated_size;
  pool.used_size = CHUNK_SIZE(chunk);
  pool.entry = chunk;

  return pool;
}

void *__tagged_malloc(uint16_t tag, size_t size) {
  DEBUG_MSG("malloc(%lu)\n", size);

  if (size == 0) {
    return NULL;
  }

  if (tag_map[tag].in_use == 0) {
    // Adjust the allocation size so that it satisfies alignment. If the
    // requested size cannot fit within the chunk's size field, return an error
    const size_t aligned_size =
        ((size + CHUNK_OVERHEAD + SIZE_ALIGN - 1) / SIZE_ALIGN) * SIZE_ALIGN;
    const size_t mmap_aligned_size =
        ((POOL_SIZE_MULTIPLIER * (size + CHUNK_OVERHEAD) + SIZE_ALIGN - 1) /
         SIZE_ALIGN) *
        SIZE_ALIGN;
    if (aligned_size > MAX_CHUNK_SIZE) {
      DEBUG_MSG("memory request too large\n");
      return NULL;
    }

    // mmap the requested memory
    DEBUG_MSG("mmap-ing %lu bytes of memory...\n", mmap_aligned_size);
    void *base = mmap(NULL, mmap_aligned_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == (void *)-1) {
      DEBUG_MSG("mmap failed\n");
      return NULL;
    }
    DEBUG_MSG("mmap base at %p\n", base);

    // Adjust the mmap'ed memory so that the memory returned to the user is
    // aligned correctly
    uintptr_t ptr = (uintptr_t)base + CHUNK_OVERHEAD;
    size_t adjust = 0;
    if ((ptr & (SIZE_ALIGN - 1)) != 0) {
      adjust = SIZE_ALIGN - (ptr & (SIZE_ALIGN - 1));
      DEBUG_MSG("adjust of %lu bytes required for mmap base\n", adjust);
    }
    ptr += adjust - CHUNK_OVERHEAD;

    // Set the required fields in the allocated chunk
    struct chunk_t *chunk = (struct chunk_t *)ptr;
    SET_CHUNK_SIZE(chunk, aligned_size);
    SET_IN_USE(chunk);

    // This is the first chunk allocated for this particular tag, so save it
    // into the tag map
    tag_map[tag] = initialize_pool(mmap_aligned_size, chunk);

    DEBUG_MSG("returning %p to the user\n", CHUNK_TO_MEM(chunk));
    return CHUNK_TO_MEM(chunk);
  } else {
    DEBUG_MSG("allocation site already in use!\n");
  }

  // Execution should not reach here
  return NULL;
}

void *__tagged_calloc(uint16_t tag, size_t nmemb, size_t size) {
  DEBUG_MSG("__tagged_calloc(%u, %lu, %lu)\n", tag, nmemb, size);

  return NULL;
}

void *malloc(size_t size) { return __tagged_malloc(DEFAULT_TAG, size); }

void *calloc(size_t nmemb, size_t size) {
  return __tagged_calloc(DEFAULT_TAG, nmemb, size);
}

void *realloc(void *ptr, size_t size) { return ptr; }

void free(void *ptr) {}
