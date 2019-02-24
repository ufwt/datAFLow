add_library(fuzzalloc-llvm MODULE TagDynamicAllocs.cpp
                                  PromoteCommon.cpp
                                  PromoteStaticArrays.cpp
                                  PromoteStaticStructs.cpp
                                  InstrumentDereferences.cpp)
