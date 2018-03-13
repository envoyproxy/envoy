#include "common/network/address_impl.h"
#include "common/request_info/utility.h"

#include "test/mocks/request_info/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::_;

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
    ON_CALL(request_info, getResponseFlag(test_case.first)).WillByDefault(Return(true));
    EXPECT_EQ(test_case.second, ResponseFlagUtils::toShortString(request_info));
  }

  // No flag is set.
  {
    NiceMock<MockRequestInfo> request_info;
    ON_CALL(request_info, getResponseFlag(_)).WillByDefault(Return(false));
    EXPECT_EQ("-", ResponseFlagUtils::toShortString(request_info));
  }

  // Test combinations.
  // These are not real use cases, but are used to cover multiple response flags case.
  {
    NiceMock<MockRequestInfo> request_info;
    ON_CALL(request_info, getResponseFlag(ResponseFlag::DelayInjected)).WillByDefault(Return(true));
    ON_CALL(request_info, getResponseFlag(ResponseFlag::FaultInjected)).WillByDefault(Return(true));
    ON_CALL(request_info, getResponseFlag(ResponseFlag::UpstreamRequestTimeout))
        .WillByDefault(Return(true));
    EXPECT_EQ("UT,DI,FI", ResponseFlagUtils::toShortString(request_info));
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
