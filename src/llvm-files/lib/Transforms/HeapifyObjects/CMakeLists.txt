include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/HeapifyUtils.cpp)

add_subdirectory(ExpandGVInitializers)
add_subdirectory(HeapifyAllocas)
add_subdirectory(HeapifyGlobalVariables)
