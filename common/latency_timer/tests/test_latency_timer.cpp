// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gtest/gtest.h>

#ifdef __linux__

#include <Robotiq/common/latency_timer.hpp>

#include <stdlib.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace Robotiq::common {
namespace {
constexpr const char* kSysfs = "sysfs";
constexpr const char* kFtdiNode = "dev/ttyUSB7";
constexpr const char* kFtdiAttribute = "sysfs/ttyUSB7/latency_timer";
constexpr const char* kFtdiLink = "dev/serial/by-id/usb-FTDI_X-if00-port0";
constexpr const char* kAcmNode = "dev/ttyACM0";

constexpr int kDefaultMs = 16;
constexpr int kLowMs = 1;

// ttyUSB7 is an FTDI adapter, ttyACM0 a node without latency_timer.
class LatencyTimer : public ::testing::Test
{
protected:
   void SetUp() override
   {
      std::string base = ::testing::TempDir() + "latency_timer_XXXXXX";
      ASSERT_NE(::mkdtemp(base.data()), nullptr);
      _root = base;
      std::filesystem::create_directories((_root / kFtdiNode).parent_path());
      std::filesystem::create_directories(std::filesystem::path(attribute()).parent_path());
      std::ofstream(_root / kFtdiNode).put('\0');
      std::ofstream(_root / kAcmNode).put('\0');
      std::ofstream(attribute()) << kDefaultMs;
   }

   void TearDown() override { std::filesystem::remove_all(_root); }

   [[nodiscard]] std::string lookup(const std::filesystem::path& port) const
   {
      return latencyTimerPath(port.string(), (_root / kSysfs).string());
   }

   [[nodiscard]] std::string attribute() const { return (_root / kFtdiAttribute).string(); }

   [[nodiscard]] int current() const
   {
      int value = -1;
      std::ifstream(attribute()) >> value;
      return value;
   }

   void makeReadOnly() const
   {
      std::filesystem::permissions(attribute(),
                                   std::filesystem::perms::owner_read | std::filesystem::perms::group_read
                                      | std::filesystem::perms::others_read);
   }

   std::filesystem::path _root;
};

TEST_F(LatencyTimer, PathFindsTheAttributeOfAnFtdiNode)
{
   EXPECT_EQ(lookup(_root / kFtdiNode), attribute());
}

TEST_F(LatencyTimer, PathFollowsSymlinksToTheDeviceNode)
{
   std::filesystem::create_directories((_root / kFtdiLink).parent_path());
   std::filesystem::create_symlink(_root / kFtdiNode, _root / kFtdiLink);

   EXPECT_EQ(lookup(_root / kFtdiLink), attribute());
}

TEST_F(LatencyTimer, PathIsEmptyForANodeWithoutTheAttribute)
{
   EXPECT_EQ(lookup(_root / kAcmNode), "");
}

TEST_F(LatencyTimer, PathIsEmptyForAMissingPort)
{
   EXPECT_EQ(lookup(_root / "dev/ttyUSB9"), "");
}

TEST_F(LatencyTimer, ApplyWritesTheValue)
{
   EXPECT_TRUE(applyLatencyTimer(attribute(), kLowMs));
   EXPECT_EQ(current(), kLowMs);
}

TEST_F(LatencyTimer, ApplyCountsAValueAlreadyInPlaceWithoutWriting)
{
   std::ofstream(attribute()) << kLowMs;
   makeReadOnly();

   EXPECT_TRUE(applyLatencyTimer(attribute(), kLowMs));
}

TEST_F(LatencyTimer, ApplyFailsWhenTheAttributeIsNotWritable)
{
   if(::geteuid() == 0)
   {
      GTEST_SKIP() << "root writes through the permissions";
   }
   makeReadOnly();

   EXPECT_FALSE(applyLatencyTimer(attribute(), kLowMs));
   EXPECT_EQ(current(), kDefaultMs);
}
} // namespace
} // namespace Robotiq::common

#endif
