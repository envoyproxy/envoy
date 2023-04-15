#include <string>

#include "source/common/access_log/utility.h"
#include "source/common/common/empty_string.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace AccessLog {
namespace {

TEST(UtilityTest, getAccessLogTypeString) {
  EXPECT_EQ(AccessLogTypeStrings::get().TcpUpstreamConnected,
            Utility::getAccessLogTypeString(AccessLogType::TcpUpstreamConnected));
  EXPECT_EQ(AccessLogTypeStrings::get().TcpPeriodic,
            Utility::getAccessLogTypeString(AccessLogType::TcpPeriodic));
  EXPECT_EQ(AccessLogTypeStrings::get().TcpEnd,
            Utility::getAccessLogTypeString(AccessLogType::TcpEnd));
  EXPECT_EQ(AccessLogTypeStrings::get().HcmNewRequest,
            Utility::getAccessLogTypeString(AccessLogType::HcmNewRequest));
  EXPECT_EQ(AccessLogTypeStrings::get().HcmPeriodic,
            Utility::getAccessLogTypeString(AccessLogType::HcmPeriodic));
  EXPECT_EQ(AccessLogTypeStrings::get().HcmEnd,
            Utility::getAccessLogTypeString(AccessLogType::HcmEnd));
  EXPECT_EQ(AccessLogTypeStrings::get().RouterNewRequest,
            Utility::getAccessLogTypeString(AccessLogType::RouterNewRequest));
  EXPECT_EQ(AccessLogTypeStrings::get().RouterPeriodic,
            Utility::getAccessLogTypeString(AccessLogType::RouterPeriodic));
  EXPECT_EQ(AccessLogTypeStrings::get().RouterEnd,
            Utility::getAccessLogTypeString(AccessLogType::RouterEnd));
  EXPECT_EQ(EMPTY_STRING, Utility::getAccessLogTypeString(AccessLogType::NotSet));
}

} // namespace
} // namespace AccessLog
} // namespace Envoy
