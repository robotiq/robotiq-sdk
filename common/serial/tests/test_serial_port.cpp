// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <Robotiq/common/serial_error.hpp>
#include <Robotiq/common/serial_port.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <Robotiq/common/serial_testing/pty_peer.hpp>
#endif

namespace Robotiq::common {
namespace {
using namespace std::chrono_literals;
using ::testing::AnyOf;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::StrEq;
using ::testing::ThrowsMessage;

#ifdef _WIN32
const std::string kMissingPort = "COM255";
#else
const std::string kMissingPort = "/dev/this_should_not_exist";
#endif

TEST(SerialPort, OpeningAMissingPortNamesIt)
{
   SerialPort port;
   EXPECT_THAT(
      [&] { port.open(kMissingPort, 115200); },
      ThrowsMessage<SerialError>(StrEq("no serial port named '" + kMissingPort + "' (is the device connected?)")));
}

TEST(SerialPort, CloseIsSafeOnAPortNeverOpened)
{
   SerialPort port;
   EXPECT_NO_THROW(port.close());
   EXPECT_NO_THROW(port.close());
}

#ifndef _WIN32
class PtySerialPort : public ::testing::Test
{
protected:
   void SetUp() override
   {
      if(!_peer.valid())
      {
         GTEST_SKIP() << "no pseudo-terminal available on this machine";
      }
      _port.open(_peer.slavePath(), 115200);
   }

   [[nodiscard]] std::vector<uint8_t> read(size_t size, std::chrono::milliseconds timeout)
   {
      std::vector<uint8_t> data(size);
      data.resize(_port.read(data.data(), data.size(), timeout));
      return data;
   }

   serial_testing::PtyPeer _peer;
   SerialPort _port;
};

TEST_F(PtySerialPort, AZeroTimeoutReadTakesOnlyWhatIsBuffered)
{
   ASSERT_TRUE(_peer.send({0xAA, 0xBB, 0xCC}));

   // The pty delivers the whole chunk to the slave's queue at once, so
   // this blocking byte also settles the two behind it.
   ASSERT_THAT(read(1, 2s), ElementsAre(0xAA));

   EXPECT_THAT(read(64, 0ms), ElementsAre(0xBB, 0xCC));
   EXPECT_THAT(read(64, 0ms), IsEmpty());
}

TEST_F(PtySerialPort, ReadWaitsForBytesThatHaveNotArrivedYet)
{
   // Repeated so some rounds find the port dry mid-frame.
   for(int round = 0; round < 32; ++round)
   {
      ASSERT_TRUE(_peer.send({0x09, 0x03}));
      std::thread tail([this] { EXPECT_TRUE(_peer.send({0x06, 0x11})); });
      const auto frame = read(4, 2s);
      tail.join();

      ASSERT_THAT(frame, ElementsAre(0x09, 0x03, 0x06, 0x11));
   }
}

TEST_F(PtySerialPort, ReadReturnsWhatArrivedByItsTimeout)
{
   const auto start = std::chrono::steady_clock::now();
   EXPECT_THAT(read(4, 50ms), IsEmpty());
   const auto elapsed = std::chrono::steady_clock::now() - start;
   EXPECT_GE(elapsed, 50ms);
   EXPECT_LT(elapsed, 5s);

   ASSERT_TRUE(_peer.send({0x09, 0x03}));
   // The pty delivers the chunk at once: the first byte settles the second.
   ASSERT_THAT(read(1, 2s), ElementsAre(0x09));
   EXPECT_THAT(read(4, 50ms), ElementsAre(0x03));
}

TEST_F(PtySerialPort, ReadRaisesWhenThePeerHangsUp)
{
   _peer.hangUp();

   EXPECT_THAT([&] { (void)read(64, 500ms); },
               ThrowsMessage<SerialError>(
                  AnyOf(HasSubstr("waiting for serial input"), HasSubstr("reading from the serial port"))));
}

TEST_F(PtySerialPort, WriteReachesThePeer)
{
   const std::vector<uint8_t> frame = {0x9A, 0x00, 0xE3, 0x00};
   std::vector<uint8_t> received;
   // A macOS pty holds written bytes until the peer reads them.
   std::thread peer([&] { received = _peer.receive(frame.size()); });
   const size_t sent = _port.write(frame.data(), frame.size(), 500ms);
   const bool drained = _port.drain(500ms);
   peer.join();

   EXPECT_EQ(sent, frame.size());
   EXPECT_TRUE(drained);
   EXPECT_EQ(received, frame);
}

TEST_F(PtySerialPort, WriteWaitsForThePeerToMakeRoom)
{
   // Far more than a pty buffers.
   const std::vector<uint8_t> data(1 << 20, 0x55);
   std::vector<uint8_t> received;
   std::thread peer([&] {
      std::this_thread::sleep_for(100ms);
      received = _peer.receive(data.size());
   });
   const size_t sent = _port.write(data.data(), data.size(), 10s);
   peer.join();

   EXPECT_EQ(sent, data.size());
   EXPECT_EQ(received, data);
}

TEST_F(PtySerialPort, WriteStopsAtItsDeadlineWhenThePeerNeverReads)
{
   // Far more than a pty buffers, with no one reading the other end.
   const std::vector<uint8_t> data(1 << 20, 0x55);

   EXPECT_LT(_port.write(data.data(), data.size(), 20ms), data.size());
}

TEST_F(PtySerialPort, WriteRaisesWhenThePeerHangsUp)
{
   _peer.hangUp();
   const std::vector<uint8_t> frame = {0x9A, 0x00, 0xE3, 0x00};

   EXPECT_THAT([&] { (void)_port.write(frame.data(), frame.size(), 500ms); },
               ThrowsMessage<SerialError>(
                  AnyOf(HasSubstr("writing to the serial port"), HasSubstr("waiting to send on the serial port"))));
}

TEST_F(PtySerialPort, OpenRejectsABaudRateThePlatformHasNoConstantFor)
{
   // The port is exclusive: free it for the second open.
   _port.close();
   SerialPort port;

   EXPECT_THAT([&] { port.open(_peer.slavePath(), 12345); },
               ThrowsMessage<SerialError>(StrEq("unsupported baud rate 12345")));
}
#endif
} // namespace
} // namespace Robotiq::common
