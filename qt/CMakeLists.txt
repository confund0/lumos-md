find_package(Qt6 6.2 QUIET COMPONENTS Widgets)
if(NOT Qt6Widgets_FOUND)
  message(WARNING "Qt6 Widgets not found: the Qt adapter and demo are not built")
  return()
endif()

add_library(lumos_qt STATIC
  src/source_editor.cpp
  src/style_table.cpp
  src/text_map.cpp
  include/lumos/qt/source_editor.hpp
  include/lumos/qt/style_table.hpp
  include/lumos/qt/text_map.hpp)
add_library(lumos::qt ALIAS lumos_qt)
set_target_properties(lumos_qt PROPERTIES AUTOMOC ON)
target_include_directories(lumos_qt PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(lumos_qt PUBLIC lumos::core Qt6::Widgets)
lumos_harden(lumos_qt)

if(LUMOS_BUILD_TESTS)
  find_package(Qt6 QUIET COMPONENTS Test)
  if(Qt6Test_FOUND)
    add_executable(lumos_qt_tests tests/test_source_editor.cpp)
    set_target_properties(lumos_qt_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(lumos_qt_tests PRIVATE lumos::qt Qt6::Test)
    target_compile_definitions(lumos_qt_tests PRIVATE
      LUMOS_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
    lumos_harden(lumos_qt_tests)
    add_test(NAME lumos_qt_tests COMMAND lumos_qt_tests)
    set_tests_properties(lumos_qt_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  else()
    message(WARNING "Qt6 Test not found: the Qt adapter tests are not built")
  endif()
endif()
