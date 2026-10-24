cmake_minimum_required(VERSION 3.21)
project(lumos VERSION 0.1.1 LANGUAGES C CXX)

if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

option(LUMOS_BUILD_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})
option(LUMOS_BUILD_QT "Build the Qt adapter" ON)
option(LUMOS_BUILD_APP "Build the lumos-md app (needs LUMOS_BUILD_QT)" ${PROJECT_IS_TOP_LEVEL})
option(LUMOS_BUILD_BENCH "Build the core benchmark (not run by ctest)" ${PROJECT_IS_TOP_LEVEL})
option(LUMOS_WERROR "Treat warnings as errors in our targets" ON)
option(LUMOS_SANITIZE "Build with sanitizers (ASan+UBSan, or UBSan trap on musl)" OFF)
set(LUMOS_CMARK_DIR "" CACHE PATH "cmark source dir for the lumos_oracle test (empty: off)")
set(LUMOS_CMARK_GFM_DIR "" CACHE PATH "cmark-gfm source dir for the lumos_oracle_gfm test (empty: off)")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(LumosHardening)
include(LumosDeps)

file(GLOB LUMOS_CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp src/md/*.cpp)
add_library(lumos_core STATIC ${LUMOS_CORE_SOURCES})
add_library(lumos::core ALIAS lumos_core)
target_compile_features(lumos_core PUBLIC cxx_std_20)
target_include_directories(lumos_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_include_directories(lumos_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
lumos_harden(lumos_core)

if(LUMOS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(LUMOS_BUILD_BENCH)
  add_subdirectory(bench)
endif()

if(LUMOS_BUILD_QT AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/qt/CMakeLists.txt")
  add_subdirectory(qt)
endif()

if(LUMOS_BUILD_APP AND TARGET lumos_qt)
  add_subdirectory(app)
endif()
