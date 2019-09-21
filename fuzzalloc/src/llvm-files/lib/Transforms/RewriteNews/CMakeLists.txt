add_llvm_loadable_module(fuzzalloc-rewrite-news
                         ${SOURCE} RewriteNews.cpp
                         PLUGIN_TOOL opt)
