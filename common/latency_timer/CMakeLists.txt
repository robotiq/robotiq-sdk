# The Linux FTDI latency_timer. See common/README.md.

add_library(robotiq_common_latency_timer_headers INTERFACE)
add_library(Robotiq::common::latency_timer_headers ALIAS robotiq_common_latency_timer_headers)
target_include_directories(robotiq_common_latency_timer_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(robotiq_common_latency_timer_headers INTERFACE cxx_std_17)

add_library(robotiq_common_latency_timer INTERFACE)
add_library(Robotiq::common::latency_timer ALIAS robotiq_common_latency_timer)
target_link_libraries(robotiq_common_latency_timer INTERFACE robotiq_common_latency_timer_headers)
# Empty outside Linux: the header and the source compile to nothing there.
target_sources(robotiq_common_latency_timer INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include/Robotiq/common/latency_timer.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/latency_timer.cpp
)
