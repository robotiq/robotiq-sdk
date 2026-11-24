add_executable(robotiq_common_serial_tests test_serial_port.cpp)
robotiq_link_common(robotiq_common_serial_tests serial)
target_link_libraries(robotiq_common_serial_tests PRIVATE
  Robotiq::common::serial_testing
  GTest::gmock
  GTest::gtest_main
  robotiq_common_warnings
)
gtest_discover_tests(robotiq_common_serial_tests)
