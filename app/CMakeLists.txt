include(GNUInstallDirs)

find_package(Qt6 6.7 REQUIRED COMPONENTS Widgets)

add_executable(lumos-md main.cpp main_window.cpp app_paths.cpp app_log.cpp find_bar.cpp recent_files.cpp toolbar_icons.cpp)
target_link_libraries(lumos-md PRIVATE lumos::qt)
target_compile_definitions(lumos-md PRIVATE LUMOS_MD_VERSION="${PROJECT_VERSION}")
lumos_harden(lumos-md)

install(TARGETS lumos-md RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES lumos-md.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES lumos-md.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES lumos-md.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

if(LUMOS_BUILD_TESTS AND TARGET Catch2::Catch2WithMain)
  add_executable(lumos_app_tests tests/test_app_log.cpp tests/test_recent_files.cpp app_log.cpp recent_files.cpp)
  target_include_directories(lumos_app_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(lumos_app_tests PRIVATE lumos::core Qt6::Core Catch2::Catch2WithMain)
  lumos_harden(lumos_app_tests)
  add_test(NAME lumos_app_tests COMMAND lumos_app_tests)
endif()
