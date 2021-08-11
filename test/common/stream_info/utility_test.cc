#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/network/address_impl.h"
#include "source/common/stream_info/utility.h"

#include "test/mocks/stream_info/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AllOf;
using testing::HasSubstr;
using testing::NiceMock;
using testing::Not;
using testing::Return;

namespace Envoy {
namespace StreamInfo {
namespace {

using envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager;

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

class ProxyStatusTest : public ::testing::Test {
protected:
  void SetUp() {
    proxy_status_config_.set_remove_details(false);
    proxy_status_config_.set_proxy_name(HttpConnectionManager::ProxyStatusConfig::ENVOY_LITERAL);
  }

  HttpConnectionManager::ProxyStatusConfig proxy_status_config_;
  NiceMock<MockStreamInfo> stream_info_;
};

TEST_F(ProxyStatusTest, ToStringNoDetailsAvailable) {
  proxy_status_config_.set_remove_details(false);
  stream_info_.response_code_details_ = absl::nullopt;
  EXPECT_THAT(ProxyStatusUtils::toString(stream_info_, ProxyStatusError::ProxyConfigurationError,
                                         /*server_name=*/"UNUSED", proxy_status_config_),
              Not(HasSubstr("details=")));
}

TEST_F(ProxyStatusTest, ToStringSetRemoveDetailsTrue) {
  proxy_status_config_.set_remove_details(true);
  stream_info_.response_code_details_ = "some_response_code_details";
  EXPECT_THAT(ProxyStatusUtils::toString(stream_info_, ProxyStatusError::ProxyConfigurationError,
                                         /*server_name=*/"UNUSED", proxy_status_config_),
              Not(HasSubstr("details=")));
}

TEST_F(ProxyStatusTest, ToStringWithDetails) {
  proxy_status_config_.set_remove_details(false);
  stream_info_.response_code_details_ = "some_response_code_details";
  EXPECT_THAT(ProxyStatusUtils::toString(stream_info_, ProxyStatusError::ProxyConfigurationError,
                                         /*server_name=*/"UNUSED", proxy_status_config_),
              HasSubstr("details='some_response_code_details'"));
}

TEST_F(ProxyStatusTest, ToStringNoServerName) {
  proxy_status_config_.set_proxy_name(HttpConnectionManager::ProxyStatusConfig::ENVOY_LITERAL);
  EXPECT_THAT(ProxyStatusUtils::toString(stream_info_, ProxyStatusError::ProxyConfigurationError,
                                         /*server_name=*/"UNUSED", proxy_status_config_),
              AllOf(HasSubstr("envoy"), Not(HasSubstr("UNUSED"))));
}

TEST_F(ProxyStatusTest, ToStringServerName) {
  proxy_status_config_.set_proxy_name(HttpConnectionManager::ProxyStatusConfig::SERVER_NAME);
  EXPECT_THAT(ProxyStatusUtils::toString(stream_info_, ProxyStatusError::ProxyConfigurationError,
                                         /*server_name=*/"foo", proxy_status_config_),
              AllOf(HasSubstr("foo"), Not(HasSubstr("envoy"))));
}

} // namespace
} // namespace StreamInfo
} // namespace Envoy
