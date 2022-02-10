add_library(fuzzalloc-expand-gv-initializers
            SHARED
            ExpandGVInitializers.cpp HeapifyUtils.cpp)
set_target_properties(fuzzalloc-expand-gv-initializers PROPERTIES LINK_FLAGS "-Wl,-znodelete")

add_library(fuzzalloc-heapify-allocas
            SHARED
            HeapifyAllocas.cpp HeapifyUtils.cpp)
set_target_properties(fuzzalloc-heapify-allocas PROPERTIES LINK_FLAGS "-Wl,-znodelete")

add_library(fuzzalloc-heapify-global-variables
            SHARED
            HeapifyGlobalVariables.cpp HeapifyUtils.cpp)
set_target_properties(fuzzalloc-heapify-global-variables PROPERTIES LINK_FLAGS "-Wl,-znodelete")
