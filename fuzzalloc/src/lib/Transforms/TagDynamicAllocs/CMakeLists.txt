include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_llvm_loadable_module(fuzzalloc-tag-dyn-allocs ${SOURCE}
                                                  TagDynamicAllocs.cpp
                                                  TagDynamicAllocsUtil.cpp
                         PLUGIN_TOOL opt)
