cmake_minimum_required(VERSION 3.21)
project(lumos_host LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)

if(NOT LUMOS_SOURCE_DIR)
  message(FATAL_ERROR "set LUMOS_SOURCE_DIR")
endif()
add_subdirectory(${LUMOS_SOURCE_DIR} lumos)

if(NOT TARGET lumos::core)
  message(FATAL_ERROR "missing target lumos::core")
endif()
foreach(_target lumos_tests lumos_spec lumos_md_replay lumos_bench lumos-md lumos_qt_tests)
  if(TARGET ${_target})
    message(FATAL_ERROR "target ${_target} must be off in a subproject")
  endif()
endforeach()

get_target_property(_features lumos_core INTERFACE_COMPILE_FEATURES)
if(NOT "cxx_std_20" IN_LIST _features)
  message(FATAL_ERROR "lumos_core does not give cxx_std_20 to its users")
endif()
