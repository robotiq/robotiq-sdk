include("${ROBOTIQ_SDK_ROOT}/cmake/RobotiqGoogleTest.cmake")

add_executable(tsf_sdk_tests test_robotiq_tactile_sensor.cpp)
target_link_libraries(tsf_sdk_tests PRIVATE
    RobotiqTactileSensor
    Robotiq::common::serial_testing
    GTest::gtest_main
)
gtest_discover_tests(tsf_sdk_tests)
