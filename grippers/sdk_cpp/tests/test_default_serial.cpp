// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <memory>

#include <Robotiq/detail/default_serial.hpp>
#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/gripper/serial_io_exception.hpp>

#ifndef _WIN32
#include <string>
#include <vector>

#include <Robotiq/common/serial_testing/pty_peer.hpp>
#endif

namespace Robotiq::test {
using detail::DefaultSerial;

namespace {
DefaultSerial makeSerial(SerialConfig config = {})
{
   return DefaultSerial(std::move(config), std::make_shared<NullLogger>());
}
} // namespace

TEST(TestDefaultSerial, throws_when_port_does_not_exist)
{
   SerialConfig config;

#ifdef _WIN32
   config.port = "COM255";
#else
   config.port = "/dev/this_should_not_exist";
#endif
   auto serial = makeSerial(config);
   EXPECT_THAT([&] { serial.open(); },
               ::testing::ThrowsMessage<SerialIOException>(::testing::StrEq(
                  "SerialIOException: no serial port named '" + config.port + "' (is the device connected?)")));
   EXPECT_FALSE(serial.isOpen());
}

TEST(TestDefaultSerial, throws_when_port_is_empty)
{
   auto serial = makeSerial();
   EXPECT_THROW(serial.open(), SerialIOException);
}

TEST(TestDefaultSerial, read_and_write_throw_on_closed_port)
{
   auto serial = makeSerial();
   EXPECT_THROW((void)serial.read(1, std::chrono::milliseconds{10}), SerialIOException);
   EXPECT_THROW(serial.write({0x01}), SerialIOException);
}

TEST(TestDefaultSerial, close_is_idempotent_on_a_never_opened_port)
{
   auto serial = makeSerial();
   EXPECT_NO_THROW(serial.close());
   EXPECT_NO_THROW(serial.close());
   EXPECT_FALSE(serial.isOpen());
}

TEST(TestDefaultSerial, construction_config_round_trip)
{
   SerialConfig config;
   config.port = "/dev/ttyUSB0";
   config.baudrate = 230400;
   config.timeout = std::chrono::milliseconds{200};
   config.latencyTimerMs = 1;
   auto serial = makeSerial(config);

   EXPECT_EQ(serial.getConfig().port, "/dev/ttyUSB0");
   EXPECT_EQ(serial.getConfig().baudrate, 230400U);
   EXPECT_EQ(serial.getConfig().timeout, std::chrono::milliseconds{200});
   EXPECT_EQ(serial.getConfig().latencyTimerMs, 1);
   EXPECT_EQ(serial.getTimeout(), std::chrono::milliseconds{200});
}

#ifndef _WIN32

using namespace std::chrono_literals;

namespace {
class PtyLoopback : public common::serial_testing::PtyPeer
{
public:
   // An open DefaultSerial on the slave end.
   [[nodiscard]] std::unique_ptr<DefaultSerial> openSerial(
      std::chrono::milliseconds timeout = SerialConfig{}.timeout) const
   {
      SerialConfig config;
      config.port = slavePath();
      config.timeout = timeout;
      auto serial = std::make_unique<DefaultSerial>(std::move(config), std::make_shared<NullLogger>());
      serial->open();
      return serial;
   }
};

class PtySerialTest : public ::testing::Test
{
protected:
   void SetUp() override
   {
      if(!_pty.valid())
      {
         GTEST_SKIP() << "no pseudo-terminal available on this machine";
      }
   }

   PtyLoopback _pty;
};
} // namespace

TEST_F(PtySerialTest, open_close_and_reopen_a_real_tty)
{
   const auto serial = _pty.openSerial();
   EXPECT_TRUE(serial->isOpen());

   serial->close();
   EXPECT_FALSE(serial->isOpen());

   EXPECT_NO_THROW(serial->open());
   EXPECT_TRUE(serial->isOpen());
}

TEST_F(PtySerialTest, opening_an_already_open_port_is_ignored)
{
   const auto serial = _pty.openSerial();
   EXPECT_NO_THROW(serial->open());
   EXPECT_TRUE(serial->isOpen());
}

#ifdef __APPLE__
// Only a macOS pty holds written bytes until the peer reads them.
TEST_F(PtySerialTest, write_times_out_when_the_peer_never_reads)
{
   const auto serial = _pty.openSerial(20ms);

   EXPECT_THAT([&] { serial->write({0x09, 0x03, 0x07, 0xD0, 0x00, 0x03}); },
               ::testing::ThrowsMessage<SerialIOException>(::testing::HasSubstr("did not drain")));
}
#endif

TEST_F(PtySerialTest, read_waits_for_the_requested_bytes)
{
   const std::vector<uint8_t> response = {0x09, 0x03, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00};
   const auto serial = _pty.openSerial();
   ASSERT_TRUE(_pty.send(response));

   EXPECT_EQ(serial->read(response.size(), 500ms), response);
}

TEST_F(PtySerialTest, a_read_error_is_a_serial_io_exception)
{
   const auto serial = _pty.openSerial();
   _pty.hangUp();

   EXPECT_THAT([&] { (void)serial->read(1, 500ms); },
               ::testing::ThrowsMessage<SerialIOException>(::testing::HasSubstr("serial")));
}

TEST_F(PtySerialTest, a_write_error_is_a_serial_io_exception)
{
   const auto serial = _pty.openSerial();
   _pty.hangUp();

   EXPECT_THAT([&] { serial->write({0x09, 0x03, 0x07, 0xD0, 0x00, 0x03}); },
               ::testing::ThrowsMessage<SerialIOException>(::testing::HasSubstr("serial")));
}

TEST_F(PtySerialTest, a_short_write_is_a_timeout)
{
   // Far more than a pty buffers, with no one reading the other end.
   const auto serial = _pty.openSerial(20ms);

   EXPECT_THAT([&] { serial->write(std::vector<uint8_t>(1 << 20, 0x55)); },
               ::testing::ThrowsMessage<SerialIOException>(::testing::HasSubstr("wrote")));
}
#endif

} // namespace Robotiq::test
