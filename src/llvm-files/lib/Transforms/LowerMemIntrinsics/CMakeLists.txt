llvm_map_components_to_libnames(LLVM_LIBS transformutils)

add_library(fuzzalloc-lower-mem-intrinsics
            SHARED
            LowerMemIntrinsics.cpp)
target_link_libraries(fuzzalloc-lower-mem-intrinsics ${LLVM_LIBS})
set_target_properties(fuzzalloc-lower-mem-intrinsics PROPERTIES LINK_FLAGS "-Wl,-znodelete")
