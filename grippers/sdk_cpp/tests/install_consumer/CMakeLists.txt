cmake_minimum_required(VERSION 3.16)

project(grippers_install_consumer LANGUAGES CXX)

find_package(grippers REQUIRED)

if(NOT TARGET Robotiq::grippers)
  message(FATAL_ERROR "find_package(grippers) did not define Robotiq::grippers")
endif()
if(NOT EXISTS "${grippers_DEVICE_PROFILES}")
  message(FATAL_ERROR "grippers_DEVICE_PROFILES does not name a file: '${grippers_DEVICE_PROFILES}'")
endif()
message(STATUS "grippers device profiles: ${grippers_DEVICE_PROFILES}")
