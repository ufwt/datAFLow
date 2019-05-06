# XXX after building and installing SVF, I had to rename lib/Svf.so to
# lib/libSvf.so so that this would compile correctly

add_llvm_loadable_module(fuzzalloc-svf-analysis SVFAnalysis.cpp
                         PLUGIN_TOOL opt
                         LINK_LIBS fuzzalloc-inst-derefs Svf)
