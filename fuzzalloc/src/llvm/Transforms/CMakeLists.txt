add_library(fuzzalloc-llvm MODULE PromoteStaticArrays.cpp
                                  PromoteStaticStructs.cpp
                                  TagDynamicAllocs.cpp
                                  InstrumentDereferences.cpp)
