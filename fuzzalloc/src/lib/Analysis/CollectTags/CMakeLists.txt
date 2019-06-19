include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_llvm_loadable_module(fuzzalloc-collect-tags ${SOURCE}
                                                CollectTags.cpp
                         PLUGIN_TOOL opt)
