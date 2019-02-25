# Same version required by LLVM 7
cmake_minimum_required(VERSION 3.4.3)

set(CMAKE_CXX_STANDARD 11)

find_package(LLVM REQUIRED CONFIG)

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

add_subdirectory(Transforms)
