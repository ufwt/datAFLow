include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(SOURCE ${SOURCE} ${CMAKE_CURRENT_SOURCE_DIR}/PromoteCommon.cpp)

add_subdirectory(ExpandGVInitializers)
add_subdirectory(PromoteAllocas)
add_subdirectory(PromoteGlobalVariables)
#add_subdirectory(PromoteStructs)
