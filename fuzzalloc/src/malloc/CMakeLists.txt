if(POOL_SIZE)
  message(STATUS "Allocation pool size set to ${POOL_SIZE} bytes")
  add_definitions(-DPOOL_SIZE=${POOL_SIZE})
endif(POOL_SIZE)

add_library(fuzzalloc SHARED malloc.c
                             deref.c)
