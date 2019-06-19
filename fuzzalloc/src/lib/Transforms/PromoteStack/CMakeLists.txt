include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(SOURCE ${SOURCE} ${CMAKE_CURRENT_SOURCE_DIR}/PromoteCommon.cpp)

add_subdirectory(PromoteArrays)
add_subdirectory(PromoteStructs)
