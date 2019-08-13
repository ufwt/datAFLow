include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(SOURCE ${SOURCE} ${CMAKE_CURRENT_SOURCE_DIR}/HeapifyCommon.cpp)

add_subdirectory(ExpandGVInitializers)
add_subdirectory(HeapifyAllocas)
add_subdirectory(HeapifyGlobalVariables)
