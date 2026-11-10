# Developer-only aggregate: opens every C++ SDK in this repository as a single
# CMake project, for IDEs that take one CMakeLists as their entry point.
#
# Not a build product. Each SDK still builds, installs and is consumed on its
# own from its own directory; nothing here affects CI or the release.
#
#   cmake --preset ide && cmake --build --preset ide && ctest --preset ide
#
# or point the IDE at dev/CMakeLists.txt and pick the "ide" preset (Qt Creator:
# Open File or Project). See CMakePresets.json beside this file.

cmake_minimum_required(VERSION 3.16)
project(robotiq_sdk_ide LANGUAGES C CXX)

# Tests registered by the SDKs run from this build directory's ctest. Only the
# grippers SDK registers any; the TSF SDK has no add_test().
enable_testing()

# Pulled in through add_subdirectory(), the grippers SDK is not top level and
# would leave out what an SDK developer works on. Turn it back on; each stays a
# cache option the IDE can flip.
set(GRIPPERS_BUILD_EXAMPLES ON CACHE BOOL "Build example programs")
set(GRIPPERS_BUILD_TESTS ON CACHE BOOL "Build the unit tests")
set(GRIPPERS_WARNINGS_AS_ERRORS ON CACHE BOOL "Treat compiler warnings as errors")

# Each SDK keeps its own project(); they sit side by side purely so the code
# model sees them together.
set(ROBOTIQ_IDE_SDKS
  grippers/sdk_cpp
)

# The TSF SDK needs libserialport, and its own find_library(... REQUIRED) would
# stop the whole configure without it. Skip TSF instead, so the grippers SDK
# still opens. Same cache variables as TSF's, so TSF reuses what is found here.
# Temporary: TSF is to drop libserialport and manage serial as the grippers SDK.
find_library(SERIALPORT_LIBRARY NAMES serialport libserialport)
find_path(SERIALPORT_INCLUDE_DIR libserialport.h)
if(SERIALPORT_LIBRARY AND SERIALPORT_INCLUDE_DIR)
  list(APPEND ROBOTIQ_IDE_SDKS tsf/sdk_cpp)
else()
  message(STATUS "robotiq_sdk_ide: libserialport not found; skipping the TSF SDK")
endif()

foreach(sdk IN LISTS ROBOTIQ_IDE_SDKS)
  string(REPLACE "/" "_" name ${sdk})
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../${sdk} ${CMAKE_CURRENT_BINARY_DIR}/${name})
endforeach()
