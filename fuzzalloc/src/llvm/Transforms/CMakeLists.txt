add_library(fuzzalloc-llvm MODULE PromoteStaticArrays.cpp
                                  TagDynamicAllocs.cpp
                                  InstrumentDereferences.cpp)
