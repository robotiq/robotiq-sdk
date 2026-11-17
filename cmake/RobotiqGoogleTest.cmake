# GoogleTest: an installed one, otherwise a pinned release. Safe to include
# from several projects of one build.

if(NOT TARGET GTest::gmock)
  find_package(GTest QUIET)
endif()
if(NOT TARGET GTest::gmock)
  include(FetchContent)
  # DOWNLOAD_EXTRACT_TIMESTAMP needs CMake 3.24; older versions read it as part of URL_HASH.
  if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
  endif()
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    URL_HASH SHA256=1f357c27ca988c3f7c6b4bf68a9395005ac6761f034046e9dde0896e3aba00e4
  )
  # For Windows: prevent overriding the parent project's runtime settings.
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()
include(GoogleTest)
