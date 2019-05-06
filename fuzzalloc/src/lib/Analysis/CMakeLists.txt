if(DEFINED ENV{SVF_PATH})
  set(SVF_PATH "$ENV{SVF_PATH}")
elseif(NOT DEFINED SVF_PATH)
  message(FATAL_ERROR "SVF_PATH not defined.
                       Set SVF_PATH to point to the SVF install directory.")
endif()

set(SVF_INCLUDE "${SVF_PATH}/include/svf")
set(SVF_LIB "${SVF_PATH}/lib")

if((NOT EXISTS ${SVF_INCLUDE}) OR (NOT EXISTS ${SVF_LIB}))
    message(FATAL_ERROR "${SVF_INCLUDE} or ${SVF_LIB} do not exist. "
                        "Please check SVF_PATH")
endif()

include_directories(${SVF_INCLUDE})
link_directories(${SVF_LIB})

add_subdirectory(SVFAnalysis)
