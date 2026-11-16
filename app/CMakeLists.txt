include(GNUInstallDirs)

find_package(Qt6 6.7 REQUIRED COMPONENTS Widgets Network)

add_executable(lumos-md main.cpp main_window.cpp document_tab.cpp app_paths.cpp app_log.cpp drafts.cpp find_bar.cpp
               link_open.cpp open_tabs.cpp recent_files.cpp toolbar_icons.cpp single_instance.cpp)
target_link_libraries(lumos-md PRIVATE lumos::qt Qt6::Network)
target_compile_definitions(lumos-md PRIVATE LUMOS_MD_VERSION="${PROJECT_VERSION}")
lumos_harden(lumos-md)

install(TARGETS lumos-md RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES lumos-md.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES lumos-md.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES lumos-md.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

if(LUMOS_BUILD_TESTS AND TARGET Catch2::Catch2WithMain)
  add_executable(lumos_app_tests tests/test_app_log.cpp tests/test_drafts.cpp tests/test_link_open.cpp
                 tests/test_open_tabs.cpp tests/test_recent_files.cpp app_log.cpp drafts.cpp link_open.cpp
                 open_tabs.cpp recent_files.cpp)
  target_include_directories(lumos_app_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(lumos_app_tests PRIVATE lumos::core Qt6::Core Catch2::Catch2WithMain)
  lumos_harden(lumos_app_tests)
  add_test(NAME lumos_app_tests COMMAND lumos_app_tests)
endif()

find_package(Qt6 QUIET COMPONENTS Test)
if(LUMOS_BUILD_TESTS AND Qt6Test_FOUND)
  add_executable(lumos_app_window_tests tests/test_main_window.cpp main_window.cpp document_tab.cpp drafts.cpp
                 find_bar.cpp link_open.cpp open_tabs.cpp recent_files.cpp toolbar_icons.cpp)
  set_target_properties(lumos_app_window_tests PROPERTIES AUTOMOC ON)
  target_include_directories(lumos_app_window_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(lumos_app_window_tests PRIVATE lumos::qt Qt6::Test)
  target_compile_definitions(lumos_app_window_tests PRIVATE LUMOS_MD_VERSION="${PROJECT_VERSION}")
  lumos_harden(lumos_app_window_tests)
  add_test(NAME lumos_app_window_tests COMMAND lumos_app_window_tests)
  set_tests_properties(lumos_app_window_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(LUMOS_BUILD_TESTS AND Qt6Test_FOUND)
  add_executable(lumos_app_drafts_tests tests/test_drafts_window.cpp main_window.cpp document_tab.cpp drafts.cpp
                 find_bar.cpp link_open.cpp open_tabs.cpp recent_files.cpp toolbar_icons.cpp)
  set_target_properties(lumos_app_drafts_tests PROPERTIES AUTOMOC ON)
  target_include_directories(lumos_app_drafts_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(lumos_app_drafts_tests PRIVATE lumos::qt Qt6::Test)
  target_compile_definitions(lumos_app_drafts_tests PRIVATE LUMOS_MD_VERSION="${PROJECT_VERSION}")
  lumos_harden(lumos_app_drafts_tests)
  add_test(NAME lumos_app_drafts_tests COMMAND lumos_app_drafts_tests)
  set_tests_properties(lumos_app_drafts_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(LUMOS_BUILD_TESTS AND Qt6Test_FOUND)
  add_executable(lumos_app_instance_tests tests/test_single_instance.cpp single_instance.cpp main_window.cpp
                 document_tab.cpp drafts.cpp find_bar.cpp link_open.cpp open_tabs.cpp recent_files.cpp
                 toolbar_icons.cpp)
  set_target_properties(lumos_app_instance_tests PROPERTIES AUTOMOC ON)
  target_include_directories(lumos_app_instance_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(lumos_app_instance_tests PRIVATE lumos::qt Qt6::Network Qt6::Test)
  target_compile_definitions(lumos_app_instance_tests PRIVATE LUMOS_MD_VERSION="${PROJECT_VERSION}")
  lumos_harden(lumos_app_instance_tests)
  add_test(NAME lumos_app_instance_tests COMMAND lumos_app_instance_tests)
  set_tests_properties(lumos_app_instance_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
