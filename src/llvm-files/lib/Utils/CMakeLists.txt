add_library(fuzzalloc-utils
            STATIC
            FuzzallocUtils.cpp)
set_target_properties(fuzzalloc-utils PROPERTIES POSITION_INDEPENDENT_CODE ON)


add_library(lower-atomics
            SHARED
            LowerAtomics.cpp)
set_target_properties(lower-atomics PROPERTIES LINK_FLAGS "-Wl,-znodelete")
