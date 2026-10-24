add_executable(lumos_bench bench_core.cpp)
target_link_libraries(lumos_bench PRIVATE lumos::core)
target_compile_definitions(lumos_bench PRIVATE
  LUMOS_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
lumos_harden(lumos_bench)
