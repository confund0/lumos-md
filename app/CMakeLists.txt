include(GNUInstallDirs)

find_package(Qt6 6.7 REQUIRED COMPONENTS Widgets Network)

file(RELATIVE_PATH LUMOS_MD_HELP_REL ${CMAKE_INSTALL_FULL_BINDIR} ${CMAKE_INSTALL_FULL_DATADIR}/lumos-md/help)
set(LUMOS_MD_HELP_BUILD ${CMAKE_CURRENT_BINARY_DIR}/${LUMOS_MD_HELP_REL})
configure_file(help/about.md.in ${LUMOS_MD_HELP_BUILD}/about.md @ONLY)
configure_file(help/features.md ${LUMOS_MD_HELP_BUILD}/features.md COPYONLY)
configure_file(help/mcp.md ${LUMOS_MD_HELP_BUILD}/mcp.md COPYONLY)
set(LUMOS_MD_DEFS LUMOS_MD_VERSION="${PROJECT_VERSION}" LUMOS_MD_HELP_DIR="${LUMOS_MD_HELP_REL}")

add_executable(lumos-md main.cpp main_window.cpp document_tab.cpp app_paths.cpp app_log.cpp drafts.cpp file_watch.cpp
               find_bar.cpp html_paste.cpp line_merge.cpp link_open.cpp mcp_bridge.cpp mcp_server.cpp merge_window.cpp
               open_tabs.cpp preview.cpp recent_files.cpp theme.cpp toolbar_icons.cpp single_instance.cpp)
target_link_libraries(lumos-md PRIVATE lumos::qt Qt6::Network)
target_compile_definitions(lumos-md PRIVATE ${LUMOS_MD_DEFS})
lumos_harden(lumos-md)

install(TARGETS lumos-md RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES lumos-md.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES lumos-md.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES lumos-md.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(FILES ${LUMOS_MD_HELP_BUILD}/about.md ${LUMOS_MD_HELP_BUILD}/features.md ${LUMOS_MD_HELP_BUILD}/mcp.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/lumos-md/help)

if(LUMOS_BUILD_TESTS AND TARGET Catch2::Catch2WithMain)
  add_executable(lumos_app_tests tests/test_app_log.cpp tests/test_drafts.cpp tests/test_file_watch.cpp
                 tests/test_line_merge.cpp tests/test_link_open.cpp tests/test_mcp_access.cpp
                 tests/test_mcp_bridge.cpp tests/test_open_tabs.cpp tests/test_recent_files.cpp app_log.cpp
                 drafts.cpp file_watch.cpp line_merge.cpp link_open.cpp mcp_bridge.cpp open_tabs.cpp
                 recent_files.cpp)
  target_include_directories(lumos_app_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(lumos_app_tests PRIVATE lumos::core Qt6::Core Qt6::Network Catch2::Catch2WithMain)
  target_compile_definitions(lumos_app_tests PRIVATE LUMOS_MD_VERSION="${PROJECT_VERSION}")
  lumos_harden(lumos_app_tests)
  add_test(NAME lumos_app_tests COMMAND lumos_app_tests)
endif()

find_package(Qt6 QUIET COMPONENTS Test)
if(LUMOS_BUILD_TESTS AND Qt6Test_FOUND)
  add_executable(lumos_app_window_tests tests/test_main_window.cpp main_window.cpp document_tab.cpp drafts.cpp
                 file_watch.cpp find_bar.cpp html_paste.cpp line_merge.cpp link_open.cpp mcp_server.cpp
                 merge_window.cpp open_tabs.cpp preview.cpp recent_files.cpp theme.cpp toolbar_icons.cpp)
  set_target_properties(lumos_app_window_tests PROPERTIES AUTOMOC ON)
  target_include_directories(lumos_app_window_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(lumos_app_window_tests PRIVATE lumos::qt Qt6::Network Qt6::Test)
  target_compile_definitions(lumos_app_window_tests PRIVATE ${LUMOS_MD_DEFS})
  lumos_harden(lumos_app_window_tests)
  add_test(NAME lumos_app_window_tests COMMAND lumos_app_window_tests)
  set_tests_properties(lumos_app_window_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(LUMOS_BUILD_TESTS AND Qt6Test_FOUND)
  add_executable(lumos_app_drafts_tests tests/test_drafts_window.cpp main_window.cpp document_tab.cpp drafts.cpp
                 file_watch.cpp find_bar.cpp html_paste.cpp line_merge.cpp link_open.cpp mcp_server.cpp
                 merge_window.cpp open_tabs.cpp preview.cpp recent_files.cpp theme.cpp toolbar_icons.cpp)
  set_target_properties(lumos_app_drafts_tests PROPERTIES AUTOMOC ON)
  target_include_directories(lumos_app_drafts_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(lumos_app_drafts_tests PRIVATE lumos::qt Qt6::Network Qt6::Test)
  target_compile_definitions(lumos_app_drafts_tests PRIVATE ${LUMOS_MD_DEFS})
  lumos_harden(lumos_app_drafts_tests)
  add_test(NAME lumos_app_drafts_tests COMMAND lumos_app_drafts_tests)
  set_tests_properties(lumos_app_drafts_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(LUMOS_BUILD_TESTS AND Qt6Test_FOUND)
  add_executable(lumos_app_watch_tests tests/test_file_watch_window.cpp main_window.cpp document_tab.cpp drafts.cpp
                 file_watch.cpp find_bar.cpp html_paste.cpp line_merge.cpp link_open.cpp mcp_server.cpp
                 merge_window.cpp open_tabs.cpp preview.cpp recent_files.cpp theme.cpp toolbar_icons.cpp)
  set_target_properties(lumos_app_watch_tests PROPERTIES AUTOMOC ON)
  target_include_directories(lumos_app_watch_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(lumos_app_watch_tests PRIVATE lumos::qt Qt6::Network Qt6::Test)
  target_compile_definitions(lumos_app_watch_tests PRIVATE ${LUMOS_MD_DEFS})
  lumos_harden(lumos_app_watch_tests)
  add_test(NAME lumos_app_watch_tests COMMAND lumos_app_watch_tests)
  set_tests_properties(lumos_app_watch_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(LUMOS_BUILD_TESTS AND Qt6Test_FOUND)
  add_executable(lumos_app_merge_tests tests/test_merge_window.cpp merge_window.cpp line_merge.cpp)
  set_target_properties(lumos_app_merge_tests PROPERTIES AUTOMOC ON)
  target_include_directories(lumos_app_merge_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(lumos_app_merge_tests PRIVATE lumos::qt Qt6::Test)
  lumos_harden(lumos_app_merge_tests)
  add_test(NAME lumos_app_merge_tests COMMAND lumos_app_merge_tests)
  set_tests_properties(lumos_app_merge_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(LUMOS_BUILD_TESTS AND Qt6Test_FOUND)
  add_executable(lumos_app_preview_tests tests/test_preview.cpp main_window.cpp document_tab.cpp drafts.cpp
                 file_watch.cpp find_bar.cpp html_paste.cpp line_merge.cpp link_open.cpp mcp_server.cpp
                 merge_window.cpp open_tabs.cpp preview.cpp recent_files.cpp theme.cpp toolbar_icons.cpp)
  set_target_properties(lumos_app_preview_tests PROPERTIES AUTOMOC ON)
  target_include_directories(lumos_app_preview_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(lumos_app_preview_tests PRIVATE lumos::qt Qt6::Network Qt6::Test)
  target_compile_definitions(lumos_app_preview_tests PRIVATE ${LUMOS_MD_DEFS}
                                                             LUMOS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  lumos_harden(lumos_app_preview_tests)
  add_test(NAME lumos_app_preview_tests COMMAND lumos_app_preview_tests)
  set_tests_properties(lumos_app_preview_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(LUMOS_BUILD_TESTS AND Qt6Test_FOUND)
  add_executable(lumos_app_instance_tests tests/test_single_instance.cpp single_instance.cpp main_window.cpp
                 document_tab.cpp drafts.cpp file_watch.cpp find_bar.cpp html_paste.cpp line_merge.cpp link_open.cpp
                 mcp_server.cpp merge_window.cpp open_tabs.cpp preview.cpp recent_files.cpp theme.cpp toolbar_icons.cpp)
  set_target_properties(lumos_app_instance_tests PROPERTIES AUTOMOC ON)
  target_include_directories(lumos_app_instance_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(lumos_app_instance_tests PRIVATE lumos::qt Qt6::Network Qt6::Test)
  target_compile_definitions(lumos_app_instance_tests PRIVATE ${LUMOS_MD_DEFS})
  lumos_harden(lumos_app_instance_tests)
  add_test(NAME lumos_app_instance_tests COMMAND lumos_app_instance_tests)
  set_tests_properties(lumos_app_instance_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(LUMOS_BUILD_TESTS AND Qt6Test_FOUND)
  add_executable(lumos_app_mcp_tests tests/test_mcp_server.cpp mcp_server.cpp mcp_bridge.cpp single_instance.cpp
                 main_window.cpp document_tab.cpp drafts.cpp file_watch.cpp find_bar.cpp html_paste.cpp
                 line_merge.cpp link_open.cpp merge_window.cpp open_tabs.cpp preview.cpp recent_files.cpp theme.cpp
                 toolbar_icons.cpp)
  set_target_properties(lumos_app_mcp_tests PROPERTIES AUTOMOC ON)
  target_include_directories(lumos_app_mcp_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(lumos_app_mcp_tests PRIVATE lumos::qt Qt6::Network Qt6::Test)
  target_compile_definitions(lumos_app_mcp_tests PRIVATE ${LUMOS_MD_DEFS}
                              LUMOS_MD_BINARY="$<TARGET_FILE:lumos-md>")
  add_dependencies(lumos_app_mcp_tests lumos-md)
  lumos_harden(lumos_app_mcp_tests)
  add_test(NAME lumos_app_mcp_tests COMMAND lumos_app_mcp_tests)
  set_tests_properties(lumos_app_mcp_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(LUMOS_BUILD_TESTS AND TARGET Catch2::Catch2)
  add_executable(lumos_app_paste_tests tests/test_html_paste.cpp html_paste.cpp)
  target_include_directories(lumos_app_paste_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(lumos_app_paste_tests PRIVATE lumos::core Qt6::Gui Catch2::Catch2)
  target_compile_definitions(lumos_app_paste_tests PRIVATE
    LUMOS_PASTE_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/paste")
  lumos_harden(lumos_app_paste_tests)
  add_test(NAME lumos_app_paste_tests COMMAND lumos_app_paste_tests)
  set_tests_properties(lumos_app_paste_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
