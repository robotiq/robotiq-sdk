cmake_minimum_required(VERSION 3.16)

# One version for the whole robotiq-sdk repository, kept in its root VERSION
# file (see the root README, "Versioning").
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/RobotiqSdkVersion.cmake" OPTIONAL
        RESULT_VARIABLE ROBOTIQ_SDK_VERSION_MODULE)
if(NOT ROBOTIQ_SDK_VERSION_MODULE)
  message(FATAL_ERROR "robotiq-sdk cmake/RobotiqSdkVersion.cmake not found: build this SDK from a full robotiq-sdk checkout")
endif()

project(RobotiqTactileSensorSDK VERSION ${ROBOTIQ_SDK_VERSION} LANGUAGES CXX)

# The SDK's own sources build as C++17, the standard of the code it shares with
# the other Robotiq SDKs. Set through CMAKE_CXX_STANDARD, so it does not
# propagate: the public headers stay C++11-clean and consumers keep their own.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Code shared with the other SDKs of robotiq-sdk (common/), compiled into this
# library rather than linked as one of its own.
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/RobotiqCommon.cmake")

# Find pthread (required for std::thread)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# SDK Library
add_library(RobotiqTactileSensor STATIC
    RobotiqTactileSensor.cpp
    RobotiqTactileSensor.h
    finger_data.h
    circular_buffer.h
)

target_include_directories(RobotiqTactileSensor
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(RobotiqTactileSensor
    PUBLIC
        Threads::Threads
)

# The OS serial transport (termios / Win32), shared with the grippers SDK.
robotiq_link_common(RobotiqTactileSensor serial)

# Quick Start Example Executable
add_executable(Quick_start
    Quick_start.cpp
)

target_link_libraries(Quick_start
    PRIVATE
        RobotiqTactileSensor
)

# Data Flow Test Executable
add_executable(test_data_flow
    test_data_flow.cpp
)

target_link_libraries(test_data_flow
    PRIVATE
        RobotiqTactileSensor
)

# Packet Analysis Test Executable
add_executable(test_packet_analysis
    test_packet_analysis.cpp
)

target_link_libraries(test_packet_analysis
    PRIVATE
        RobotiqTactileSensor
)

# Installation rules
install(TARGETS RobotiqTactileSensor Quick_start
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES
    RobotiqTactileSensor.h
    finger_data.h
    circular_buffer.h
    DESTINATION include/RobotiqTactileSensor
)

# Print build information
message(STATUS "========================================")
message(STATUS "Robotiq Tactile Sensor C++ SDK")
message(STATUS "========================================")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "========================================")
