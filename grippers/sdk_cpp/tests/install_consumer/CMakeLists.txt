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

# Off for a freestanding install, which has no DefaultSerial to link.
option(LINK_DEFAULT_SERIAL "Link a Gripper on the OS serial port" OFF)
if(LINK_DEFAULT_SERIAL)
  add_executable(consumer consumer.cpp)
  target_link_libraries(consumer PRIVATE Robotiq::grippers)
endif()
