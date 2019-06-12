include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(SOURCE ${SOURCE} ${CMAKE_CURRENT_SOURCE_DIR}/Utils.cpp)

add_subdirectory(InstrumentDerefs)
add_subdirectory(PromoteStack)
add_subdirectory(TagDynamicAllocs)
