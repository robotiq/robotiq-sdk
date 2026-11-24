# The serial transport the SDKs share. See common/README.md.

# For code that calls the module without compiling it (an SDK's tests).
add_library(robotiq_common_serial_headers INTERFACE)
add_library(Robotiq::common::serial_headers ALIAS robotiq_common_serial_headers)
target_include_directories(robotiq_common_serial_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(robotiq_common_serial_headers INTERFACE cxx_std_17)
target_link_libraries(robotiq_common_serial_headers INTERFACE robotiq_common_abi)

add_library(robotiq_common_serial INTERFACE)
add_library(Robotiq::common::serial ALIAS robotiq_common_serial)
target_link_libraries(robotiq_common_serial INTERFACE robotiq_common_serial_headers)

target_sources(robotiq_common_serial INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include/Robotiq/common/serial_port.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/Robotiq/common/serial_error.hpp
)
if(WIN32)
  target_sources(robotiq_common_serial INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/win32/serial_port.cpp)
else()
  target_sources(robotiq_common_serial INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/posix/serial_port.cpp)
endif()

# A pseudo-terminal standing in for a device in tests (POSIX).
add_library(robotiq_common_serial_testing INTERFACE)
add_library(Robotiq::common::serial_testing ALIAS robotiq_common_serial_testing)
target_include_directories(robotiq_common_serial_testing INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/testing)
target_compile_features(robotiq_common_serial_testing INTERFACE cxx_std_17)
