#include <string>

#include "source/common/access_log/utility.h"
#include "source/common/common/empty_string.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace AccessLog {
namespace {

TEST(UtilityTest, getAccessLogTypeString) {
  EXPECT_EQ(AccessLogTypeStrings::get().NotSet,
            Utility::getAccessLogTypeString(AccessLogType::NotSet));
  EXPECT_EQ(AccessLogTypeStrings::get().TcpUpstreamConnected,
            Utility::getAccessLogTypeString(AccessLogType::TcpUpstreamConnected));
  EXPECT_EQ(AccessLogTypeStrings::get().TcpPeriodic,
            Utility::getAccessLogTypeString(AccessLogType::TcpPeriodic));
  EXPECT_EQ(AccessLogTypeStrings::get().TcpEnd,
            Utility::getAccessLogTypeString(AccessLogType::TcpEnd));
  EXPECT_EQ(AccessLogTypeStrings::get().DownstreamStart,
            Utility::getAccessLogTypeString(AccessLogType::DownstreamStart));
  EXPECT_EQ(AccessLogTypeStrings::get().DownstreamPeriodic,
            Utility::getAccessLogTypeString(AccessLogType::DownstreamPeriodic));
  EXPECT_EQ(AccessLogTypeStrings::get().DownstreamEnd,
            Utility::getAccessLogTypeString(AccessLogType::DownstreamEnd));
  EXPECT_EQ(AccessLogTypeStrings::get().UpstreamStart,
            Utility::getAccessLogTypeString(AccessLogType::UpstreamStart));
  EXPECT_EQ(AccessLogTypeStrings::get().UpstreamPeriodic,
            Utility::getAccessLogTypeString(AccessLogType::UpstreamPeriodic));
  EXPECT_EQ(AccessLogTypeStrings::get().UpstreamEnd,
            Utility::getAccessLogTypeString(AccessLogType::UpstreamEnd));
}

TEST(UtilityTest, getAccessLogTypeProto) {
  EXPECT_EQ(AccessLogTypeProto::NotSet, Utility::getAccessLogTypeProto(AccessLogType::NotSet));
  EXPECT_EQ(AccessLogTypeProto::TcpUpstreamConnected,
            Utility::getAccessLogTypeProto(AccessLogType::TcpUpstreamConnected));
  EXPECT_EQ(AccessLogTypeProto::TcpPeriodic,
            Utility::getAccessLogTypeProto(AccessLogType::TcpPeriodic));
  EXPECT_EQ(AccessLogTypeProto::TcpEnd, Utility::getAccessLogTypeProto(AccessLogType::TcpEnd));
  EXPECT_EQ(AccessLogTypeProto::DownstreamStart,
            Utility::getAccessLogTypeProto(AccessLogType::DownstreamStart));
  EXPECT_EQ(AccessLogTypeProto::DownstreamPeriodic,
            Utility::getAccessLogTypeProto(AccessLogType::DownstreamPeriodic));
  EXPECT_EQ(AccessLogTypeProto::DownstreamEnd,
            Utility::getAccessLogTypeProto(AccessLogType::DownstreamEnd));
  EXPECT_EQ(AccessLogTypeProto::UpstreamStart,
            Utility::getAccessLogTypeProto(AccessLogType::UpstreamStart));
  EXPECT_EQ(AccessLogTypeProto::UpstreamPeriodic,
            Utility::getAccessLogTypeProto(AccessLogType::UpstreamPeriodic));
  EXPECT_EQ(AccessLogTypeProto::UpstreamEnd,
            Utility::getAccessLogTypeProto(AccessLogType::UpstreamEnd));
}

} // namespace
} // namespace AccessLog
} // namespace Envoy
