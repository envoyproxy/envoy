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
  static_assert(ResponseFlag::LastFlag == 0x200000, "A flag has been added. Fix this code.");

  std::vector<std::pair<ResponseFlag, std::string>> expected = {
      std::make_pair(ResponseFlag::FailedLocalHealthCheck, "LH"),
      std::make_pair(ResponseFlag::NoHealthyUpstream, "UH"),
      std::make_pair(ResponseFlag::UpstreamRequestTimeout, "UT"),
      std::make_pair(ResponseFlag::LocalReset, "LR"),
      std::make_pair(ResponseFlag::UpstreamRemoteReset, "UR"),
      std::make_pair(ResponseFlag::UpstreamConnectionFailure, "UF"),
      std::make_pair(ResponseFlag::UpstreamConnectionTermination, "UC"),
      std::make_pair(ResponseFlag::UpstreamOverflow, "UO"),
      std::make_pair(ResponseFlag::NoRouteFound, "NR"),
      std::make_pair(ResponseFlag::DelayInjected, "DI"),
      std::make_pair(ResponseFlag::FaultInjected, "FI"),
      std::make_pair(ResponseFlag::RateLimited, "RL"),
      std::make_pair(ResponseFlag::UnauthorizedExternalService, "UAEX"),
      std::make_pair(ResponseFlag::RateLimitServiceError, "RLSE"),
      std::make_pair(ResponseFlag::DownstreamConnectionTermination, "DC"),
      std::make_pair(ResponseFlag::UpstreamRetryLimitExceeded, "URX"),
      std::make_pair(ResponseFlag::StreamIdleTimeout, "SI"),
      std::make_pair(ResponseFlag::InvalidEnvoyRequestHeaders, "IH"),
      std::make_pair(ResponseFlag::DownstreamProtocolError, "DPE"),
      std::make_pair(ResponseFlag::UpstreamMaxStreamDurationReached, "UMSDR"),
      std::make_pair(ResponseFlag::ResponseFromCacheFilter, "RFCF"),
      std::make_pair(ResponseFlag::NoFilterConfigFound, "NFCF")};

  for (const auto& test_case : expected) {
    NiceMock<MockStreamInfo> stream_info;
    ON_CALL(stream_info, hasResponseFlag(test_case.first)).WillByDefault(Return(true));
    EXPECT_EQ(test_case.second, ResponseFlagUtils::toShortString(stream_info));
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
  static_assert(ResponseFlag::LastFlag == 0x200000, "A flag has been added. Fix this code.");

  std::vector<std::pair<std::string, ResponseFlag>> expected = {
      std::make_pair("LH", ResponseFlag::FailedLocalHealthCheck),
      std::make_pair("UH", ResponseFlag::NoHealthyUpstream),
      std::make_pair("UT", ResponseFlag::UpstreamRequestTimeout),
      std::make_pair("LR", ResponseFlag::LocalReset),
      std::make_pair("UR", ResponseFlag::UpstreamRemoteReset),
      std::make_pair("UF", ResponseFlag::UpstreamConnectionFailure),
      std::make_pair("UC", ResponseFlag::UpstreamConnectionTermination),
      std::make_pair("UO", ResponseFlag::UpstreamOverflow),
      std::make_pair("NR", ResponseFlag::NoRouteFound),
      std::make_pair("DI", ResponseFlag::DelayInjected),
      std::make_pair("FI", ResponseFlag::FaultInjected),
      std::make_pair("RL", ResponseFlag::RateLimited),
      std::make_pair("UAEX", ResponseFlag::UnauthorizedExternalService),
      std::make_pair("RLSE", ResponseFlag::RateLimitServiceError),
      std::make_pair("DC", ResponseFlag::DownstreamConnectionTermination),
      std::make_pair("URX", ResponseFlag::UpstreamRetryLimitExceeded),
      std::make_pair("SI", ResponseFlag::StreamIdleTimeout),
      std::make_pair("IH", ResponseFlag::InvalidEnvoyRequestHeaders),
      std::make_pair("DPE", ResponseFlag::DownstreamProtocolError),
      std::make_pair("UMSDR", ResponseFlag::UpstreamMaxStreamDurationReached),
      std::make_pair("RFCF", ResponseFlag::ResponseFromCacheFilter),
      std::make_pair("NFCF", ResponseFlag::NoFilterConfigFound)};

  EXPECT_FALSE(ResponseFlagUtils::toResponseFlag("NonExistentFlag").has_value());

  for (const auto& test_case : expected) {
    absl::optional<ResponseFlag> response_flag = ResponseFlagUtils::toResponseFlag(test_case.first);
    EXPECT_TRUE(response_flag.has_value());
    EXPECT_EQ(test_case.second, response_flag.value());
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
