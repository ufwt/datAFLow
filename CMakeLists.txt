# Same version required by LLVM
cmake_minimum_required(VERSION 3.13.4)

project(datAFLow
  LANGUAGES C CXX
  DESCRIPTION "Data-flow-based fuzzing"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror -fno-rtti")

add_subdirectory(src)
