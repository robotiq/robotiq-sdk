# The tests of the common/ modules. SDKs use robotiq_link_common() instead.

cmake_minimum_required(VERSION 3.16)
project(robotiq_common_tests LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/RobotiqCommon.cmake")
include("${ROBOTIQ_SDK_ROOT}/cmake/RobotiqGoogleTest.cmake")
include("${ROBOTIQ_SDK_ROOT}/cmake/RobotiqWarnings.cmake")
robotiq_add_warnings_target(robotiq_common_warnings ON)
enable_testing()

add_subdirectory(serial/tests)
add_subdirectory(latency_timer/tests)
