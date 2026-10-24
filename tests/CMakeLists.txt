file(GLOB LUMOS_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
list(REMOVE_ITEM LUMOS_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_spec.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/md_replay.cpp)
add_executable(lumos_tests ${LUMOS_TEST_SOURCES})
target_link_libraries(lumos_tests PRIVATE lumos::core Catch2::Catch2WithMain)
target_include_directories(lumos_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(lumos_tests PRIVATE
  LUMOS_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
  LUMOS_SPEC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/spec")
lumos_harden(lumos_tests)

add_test(NAME lumos_tests COMMAND lumos_tests "~[random]~[pathological]")
add_test(NAME lumos_random_sample COMMAND lumos_tests "parser: apply_edit with random edits over the sample")
add_test(NAME lumos_random_larger COMMAND lumos_tests "parser: apply_edit with random edits over a larger document")
add_test(NAME lumos_random_blocks COMMAND lumos_tests "parser: apply_edit with random edits over random block documents")
add_test(NAME lumos_random_crlf COMMAND lumos_tests "parser: apply_edit with random edits over CRLF text")
add_test(NAME lumos_random_commands COMMAND lumos_tests "commands: property check over random documents")
add_test(NAME lumos_random_toggles COMMAND lumos_tests
  "commands: property check of the toggles over random documents")
add_test(NAME lumos_random_blocks_cmds COMMAND lumos_tests
  "commands: property check of the block commands over random documents")
add_test(NAME lumos_random_table_cmds COMMAND lumos_tests
  "commands: property check of the table commands over random documents")
add_test(NAME lumos_random_find COMMAND lumos_tests
  "commands: property check of find and replace over random documents")
add_test(NAME lumos_md_pathological COMMAND lumos_tests "[pathological]")

add_test(NAME lumos_random_md_sample COMMAND lumos_tests
  "md incremental: apply_edit matches a full parse over the sample")
add_test(NAME lumos_random_md_blocks COMMAND lumos_tests
  "md incremental: apply_edit matches a full parse over a block-heavy document")
add_test(NAME lumos_random_md_crlf COMMAND lumos_tests
  "md incremental: apply_edit matches a full parse over CRLF text")
add_test(NAME lumos_random_md_spec COMMAND lumos_tests
  "md incremental: spec examples built and edited one by one")
add_test(NAME lumos_random_md_links COMMAND lumos_tests
  "md incremental: apply_edit matches a full parse over a link-heavy document")
add_test(NAME lumos_random_md_tables COMMAND lumos_tests
  "md incremental: apply_edit matches a full parse over a table-heavy document")
add_test(NAME lumos_random_md_tasks COMMAND lumos_tests
  "md incremental: apply_edit matches a full parse over a task and front matter document")
add_test(NAME lumos_random_md_autolinks COMMAND lumos_tests
  "md incremental: apply_edit matches a full parse over an autolink document")
add_test(NAME lumos_md_locality COMMAND lumos_tests
  "md incremental: locality of typing in a paragraph and a list item")

add_executable(lumos_spec test_spec.cpp)
target_link_libraries(lumos_spec PRIVATE lumos::core)
target_include_directories(lumos_spec PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(lumos_spec PRIVATE LUMOS_SPEC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/spec")
lumos_harden(lumos_spec)
add_test(NAME lumos_spec COMMAND lumos_spec)

add_executable(lumos_md_replay md_replay.cpp)
target_link_libraries(lumos_md_replay PRIVATE lumos::core)
target_include_directories(lumos_md_replay PRIVATE ${PROJECT_SOURCE_DIR}/src)
lumos_harden(lumos_md_replay)

add_test(NAME lumos_add_subdirectory COMMAND ${CMAKE_COMMAND}
  -S ${CMAKE_CURRENT_SOURCE_DIR}/add_subdirectory
  -B ${CMAKE_CURRENT_BINARY_DIR}/add_subdirectory
  -G ${CMAKE_GENERATOR}
  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
  -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
  -DLUMOS_SOURCE_DIR=${PROJECT_SOURCE_DIR})

find_package(Python3 COMPONENTS Interpreter QUIET)
set(_casefold "${PROJECT_SOURCE_DIR}/tmp/reference/cmark/data/CaseFolding.txt")
if(Python3_Interpreter_FOUND AND EXISTS "${_casefold}")
  add_test(NAME lumos_unicode_tables COMMAND ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tools/gen_unicode_tables.py --check "${_casefold}"
    ${PROJECT_SOURCE_DIR}/src/md/unicode_tables.inc)
  set_tests_properties(lumos_unicode_tables PROPERTIES SKIP_RETURN_CODE 77)
endif()
if(Python3_Interpreter_FOUND)
  add_test(NAME lumos_entity_table COMMAND ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tools/gen_entity_table.py --check ${PROJECT_SOURCE_DIR}/src/md/entity_table.inc)
endif()

if(LUMOS_CMARK_DIR OR LUMOS_CMARK_GFM_DIR)
  add_subdirectory(oracle)
endif()
