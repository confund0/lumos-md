include(FetchContent)

if(LUMOS_BUILD_TESTS)
  find_package(Catch2 3 QUIET)
  if(Catch2_FOUND)
    message(STATUS "lumos: Catch2 ${Catch2_VERSION} from the system")
  else()
    message(STATUS "lumos: Catch2 v3.16.1 from GitHub (no system Catch2 3)")
    FetchContent_Declare(Catch2
      GIT_REPOSITORY https://github.com/catchorg/Catch2.git
      GIT_TAG v3.16.1
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)
  endif()
endif()
