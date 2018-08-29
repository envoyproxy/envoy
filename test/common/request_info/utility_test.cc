#include "common/network/address_impl.h"
#include "common/request_info/utility.h"

#include "test/mocks/request_info/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace RequestInfo {

TEST(ResponseFlagUtilsTest, toShortStringConversion) {
  static_assert(ResponseFlag::LastFlag == 0x1000, "A flag has been added. Fix this code.");

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
  };

  for (const auto& test_case : expected) {
    NiceMock<MockRequestInfo> request_info;
    ON_CALL(request_info, hasResponseFlag(test_case.first)).WillByDefault(Return(true));
    EXPECT_EQ(test_case.second, ResponseFlagUtils::toShortString(request_info));
  }

  // No flag is set.
  {
    NiceMock<MockRequestInfo> request_info;
    ON_CALL(request_info, hasResponseFlag(_)).WillByDefault(Return(false));
    EXPECT_EQ("-", ResponseFlagUtils::toShortString(request_info));
  }

  // Test combinations.
  // These are not real use cases, but are used to cover multiple response flags case.
  {
    NiceMock<MockRequestInfo> request_info;
    ON_CALL(request_info, hasResponseFlag(ResponseFlag::DelayInjected)).WillByDefault(Return(true));
    ON_CALL(request_info, hasResponseFlag(ResponseFlag::FaultInjected)).WillByDefault(Return(true));
    ON_CALL(request_info, hasResponseFlag(ResponseFlag::UpstreamRequestTimeout))
        .WillByDefault(Return(true));
    EXPECT_EQ("UT,DI,FI", ResponseFlagUtils::toShortString(request_info));
  }
}

TEST(ResponseFlagsUtilsTest, toResponseFlagConversion) {
  static_assert(ResponseFlag::LastFlag == 0x1000, "A flag has been added. Fix this code.");

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
  };

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

} // namespace RequestInfo
} // namespace Envoy
