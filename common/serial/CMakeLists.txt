# The OS serial transport every Robotiq SDK shares: SerialPort (termios on
# Linux and macOS, the Win32 comm API on Windows), the SerialError it throws,
# and the Linux FTDI latency_timer helpers.
#
# Added through robotiq_link_common() (cmake/RobotiqCommon.cmake), never on
# its own. It defines no library to build: the sources compile into each SDK
# that links Robotiq::common::serial, with that SDK's flags, so nothing here
# is installed or exported.

# The headers alone, for code that calls into the module without being the
# library that compiles it (an SDK's unit tests).
add_library(robotiq_common_serial_headers INTERFACE)
add_library(Robotiq::common::serial_headers ALIAS robotiq_common_serial_headers)
target_include_directories(robotiq_common_serial_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(robotiq_common_serial_headers INTERFACE cxx_std_17)

add_library(robotiq_common_serial INTERFACE)
add_library(Robotiq::common::serial ALIAS robotiq_common_serial)
target_link_libraries(robotiq_common_serial INTERFACE robotiq_common_serial_headers)

target_sources(robotiq_common_serial INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include/Robotiq/common/serial_port.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/Robotiq/common/serial_error.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/Robotiq/common/latency_timer.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/latency_timer.cpp
)
# One translation unit per platform family.
if(WIN32)
  target_sources(robotiq_common_serial INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/win32/serial_port.cpp)
else()
  target_sources(robotiq_common_serial INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/posix/serial_port.cpp)
endif()
