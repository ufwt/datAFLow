find_package(LLVM REQUIRED CONFIG)

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(Analysis)
add_subdirectory(Transforms)
add_subdirectory(Utils)
