include(ExternalProject)

set(_ref_args
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
  -DCMAKE_INSTALL_LIBDIR=lib
  -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
  -DBUILD_TESTING=OFF)

if(LUMOS_CMARK_DIR)
  ExternalProject_Add(cmark_ref
    SOURCE_DIR "${LUMOS_CMARK_DIR}"
    PREFIX "${CMAKE_CURRENT_BINARY_DIR}/cmark"
    CMAKE_ARGS ${_ref_args} -DBUILD_SHARED_LIBS=OFF -DCMARK_LIB_FUZZER=OFF
    BUILD_BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/cmark/lib/libcmark.a"
    UPDATE_COMMAND "" TEST_COMMAND "")
  add_executable(lumos_oracle test_oracle.cpp)
  add_dependencies(lumos_oracle cmark_ref)
  target_include_directories(lumos_oracle SYSTEM PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/cmark/include")
  target_compile_definitions(lumos_oracle PRIVATE LUMOS_ORACLE_CMARK CMARK_STATIC_DEFINE)
  target_link_libraries(lumos_oracle PRIVATE lumos::core "${CMAKE_CURRENT_BINARY_DIR}/cmark/lib/libcmark.a")
  lumos_harden(lumos_oracle)
  add_test(NAME lumos_oracle COMMAND lumos_oracle)
  add_test(NAME lumos_oracle_safe COMMAND lumos_oracle --safe --seed 2)
endif()

if(LUMOS_CMARK_GFM_DIR)
  set(_gfm "${CMAKE_CURRENT_BINARY_DIR}/cmark-gfm")
  ExternalProject_Add(cmark_gfm_ref
    SOURCE_DIR "${LUMOS_CMARK_GFM_DIR}"
    PREFIX "${_gfm}"
    CMAKE_ARGS ${_ref_args} -DCMARK_TESTS=OFF -DCMARK_SHARED=OFF -DCMARK_STATIC=ON -DCMARK_LIB_FUZZER=OFF
    BUILD_BYPRODUCTS "${_gfm}/lib/libcmark-gfm-extensions.a" "${_gfm}/lib/libcmark-gfm.a"
    UPDATE_COMMAND "" TEST_COMMAND "")
  add_executable(lumos_oracle_gfm test_oracle.cpp)
  add_dependencies(lumos_oracle_gfm cmark_gfm_ref)
  target_include_directories(lumos_oracle_gfm SYSTEM PRIVATE "${_gfm}/include")
  target_compile_definitions(lumos_oracle_gfm PRIVATE LUMOS_ORACLE_GFM CMARK_GFM_STATIC_DEFINE
    CMARK_GFM_EXTENSIONS_STATIC_DEFINE)
  target_link_libraries(lumos_oracle_gfm PRIVATE lumos::core
    "${_gfm}/lib/libcmark-gfm-extensions.a" "${_gfm}/lib/libcmark-gfm.a")
  lumos_harden(lumos_oracle_gfm)
  add_test(NAME lumos_oracle_gfm COMMAND lumos_oracle_gfm)
  add_test(NAME lumos_oracle_gfm_safe COMMAND lumos_oracle_gfm --safe --seed 2)
endif()
