# Same version required by LLVM 7
cmake_minimum_required(VERSION 3.4.3)

project(fuzzalloc LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror")

add_subdirectory(src)
