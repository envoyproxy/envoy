#include "common/network/address_impl.h"
#include "common/stream_info/utility.h"

#include "test/mocks/stream_info/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace StreamInfo {
namespace {

TEST(ResponseFlagUtilsTest, toShortStringConversion) {
  for (const auto& [flag_string, flag_enum] : ResponseFlagUtils::ALL_RESPONSE_STRING_FLAGS) {
    NiceMock<MockStreamInfo> stream_info;
    ON_CALL(stream_info, hasResponseFlag(flag_enum)).WillByDefault(Return(true));
    EXPECT_EQ(flag_string, ResponseFlagUtils::toShortString(stream_info));
  }

  // No flag is set.
  {
    NiceMock<MockStreamInfo> stream_info;
    ON_CALL(stream_info, hasResponseFlag(_)).WillByDefault(Return(false));
    EXPECT_EQ("-", ResponseFlagUtils::toShortString(stream_info));
  }

  // Test combinations.
  // These are not real use cases, but are used to cover multiple response flags case.
  {
    NiceMock<MockStreamInfo> stream_info;
    ON_CALL(stream_info, hasResponseFlag(ResponseFlag::DelayInjected)).WillByDefault(Return(true));
    ON_CALL(stream_info, hasResponseFlag(ResponseFlag::FaultInjected)).WillByDefault(Return(true));
    ON_CALL(stream_info, hasResponseFlag(ResponseFlag::UpstreamRequestTimeout))
        .WillByDefault(Return(true));
    EXPECT_EQ("UT,DI,FI", ResponseFlagUtils::toShortString(stream_info));
  }
}

TEST(ResponseFlagsUtilsTest, toResponseFlagConversion) {
  EXPECT_FALSE(ResponseFlagUtils::toResponseFlag("NonExistentFlag").has_value());

  for (const auto& [flag_string, flag_enum] : ResponseFlagUtils::ALL_RESPONSE_STRING_FLAGS) {
    absl::optional<ResponseFlag> response_flag = ResponseFlagUtils::toResponseFlag(flag_string);
    EXPECT_TRUE(response_flag.has_value());
    EXPECT_EQ(flag_enum, response_flag.value());
  }
}

TEST(UtilityTest, formatDownstreamAddressNoPort) {
  EXPECT_EQ("1.2.3.4",
            Utility::formatDownstreamAddressNoPort(Network::Address::Ipv4Instance("1.2.3.4")));
  EXPECT_EQ("/hello",
            Utility::formatDownstreamAddressNoPort(Network::Address::PipeInstance("/hello")));
}

} // namespace
} // namespace StreamInfo
} // namespace Envoy
