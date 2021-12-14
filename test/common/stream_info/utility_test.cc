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
  void SetUp() override {
    proxy_status_config_.set_remove_details(false);
    proxy_status_config_.set_proxy_name(HttpConnectionManager::ProxyStatusConfig::ENVOY_LITERAL);

    ON_CALL(stream_info_, hasAnyResponseFlag()).WillByDefault(Return(true));
    ON_CALL(stream_info_, hasResponseFlag(ResponseFlag::DelayInjected)).WillByDefault(Return(true));
  }

  HttpConnectionManager::ProxyStatusConfig proxy_status_config_;
  NiceMock<MockStreamInfo> stream_info_;
};

TEST_F(ProxyStatusTest, ToStringAbsentDetails) {
  proxy_status_config_.set_remove_details(false);
  proxy_status_config_.set_remove_connection_termination_details(false);
  proxy_status_config_.set_remove_response_flags(false);
  stream_info_.response_code_details_ = absl::nullopt;
  stream_info_.connection_termination_details_ = absl::nullopt;
  EXPECT_THAT(ProxyStatusUtils::toString(stream_info_, ProxyStatusError::ProxyConfigurationError,
                                         /*node_id=*/"UNUSED", proxy_status_config_),
              Not(HasSubstr("details=")));
}

TEST_F(ProxyStatusTest, ToStringNoDetails) {
  proxy_status_config_.set_remove_details(true);
  proxy_status_config_.set_remove_connection_termination_details(false);
  proxy_status_config_.set_remove_response_flags(false);
  stream_info_.response_code_details_ = "some_response_code_details";
  stream_info_.connection_termination_details_ = "some_connection_termination_details";
  EXPECT_THAT(ProxyStatusUtils::toString(stream_info_, ProxyStatusError::ProxyConfigurationError,
                                         /*node_id=*/"UNUSED", proxy_status_config_),
              AllOf(Not(HasSubstr("details=")), Not(HasSubstr("some_response_code_details")),
                    Not(HasSubstr("some_connection_termination_details"))));
}

TEST_F(ProxyStatusTest, ToStringWithAllFields) {
  proxy_status_config_.set_remove_details(false);
  proxy_status_config_.set_remove_connection_termination_details(false);
  proxy_status_config_.set_remove_response_flags(false);
  stream_info_.response_code_details_ = "some_response_code_details";
  stream_info_.connection_termination_details_ = "some_connection_termination_details";
  EXPECT_THAT(
      ProxyStatusUtils::toString(stream_info_, ProxyStatusError::ProxyConfigurationError,
                                 /*node_id=*/"UNUSED", proxy_status_config_),
      HasSubstr("details=\"some_response_code_details; some_connection_termination_details; DI\""));
}

TEST_F(ProxyStatusTest, ToStringWithAllFieldsVerifyStringEscape) {
  proxy_status_config_.set_remove_details(false);
  proxy_status_config_.set_remove_connection_termination_details(false);
  proxy_status_config_.set_remove_response_flags(false);
  stream_info_.response_code_details_ = "some \"response\" code details";
  stream_info_.connection_termination_details_ = "some \"connection\" termination details";
  EXPECT_THAT(ProxyStatusUtils::toString(stream_info_, ProxyStatusError::ProxyConfigurationError,
                                         /*node_id=*/"UNUSED", proxy_status_config_),
              HasSubstr("details=\"some \\\"response\\\" code details; some "
                        "\\\"connection\\\" termination details; DI\""));
}

TEST_F(ProxyStatusTest, ToStringNoConnectionTerminationDetails) {
  proxy_status_config_.set_remove_details(false);
  proxy_status_config_.set_remove_connection_termination_details(true);
  proxy_status_config_.set_remove_response_flags(false);
  stream_info_.response_code_details_ = "some_response_code_details";
  stream_info_.connection_termination_details_ = "some_connection_termination_details";
  EXPECT_THAT(ProxyStatusUtils::toString(stream_info_, ProxyStatusError::ProxyConfigurationError,
                                         /*node_id=*/"UNUSED", proxy_status_config_),
              AllOf(HasSubstr("details=\"some_response_code_details; DI\""),
                    Not(HasSubstr("some_connection_termination_details"))));
}

TEST_F(ProxyStatusTest, ToStringAbsentConnectionTerminationDetails) {
  proxy_status_config_.set_remove_details(false);
  proxy_status_config_.set_remove_connection_termination_details(false); // Don't remove them,
  proxy_status_config_.set_remove_response_flags(false);
  stream_info_.response_code_details_ = "some_response_code_details";
  stream_info_.connection_termination_details_ = absl::nullopt; // But they're absent,
  EXPECT_THAT(
      ProxyStatusUtils::toString(stream_info_, ProxyStatusError::ProxyConfigurationError,
                                 /*node_id=*/"UNUSED", proxy_status_config_),
      HasSubstr("details=\"some_response_code_details; DI\"")); // So they shouldn't be printed.
}

TEST_F(ProxyStatusTest, ToStringNoResponseFlags) {
  proxy_status_config_.set_remove_details(false);
  proxy_status_config_.set_remove_connection_termination_details(false);
  proxy_status_config_.set_remove_response_flags(true);
  stream_info_.response_code_details_ = "some_response_code_details";
  stream_info_.connection_termination_details_ = "some_connection_termination_details";
  EXPECT_THAT(
      ProxyStatusUtils::toString(stream_info_, ProxyStatusError::ProxyConfigurationError,
                                 /*node_id=*/"UNUSED", proxy_status_config_),
      AllOf(
          HasSubstr("details=\"some_response_code_details; some_connection_termination_details\""),
          Not(HasSubstr("DI"))));
}

TEST_F(ProxyStatusTest, ToStringAbsentResponseFlags) {
  proxy_status_config_.set_remove_details(false);
  proxy_status_config_.set_remove_connection_termination_details(false);
  proxy_status_config_.set_remove_response_flags(false);
  stream_info_.response_code_details_ = "some_response_code_details";
  stream_info_.connection_termination_details_ = "some_connection_termination_details";
  ON_CALL(stream_info_, hasAnyResponseFlag()).WillByDefault(Return(false));
  ON_CALL(stream_info_, hasResponseFlag(_)).WillByDefault(Return(false));
  EXPECT_THAT(
      ProxyStatusUtils::toString(stream_info_, ProxyStatusError::ProxyConfigurationError,
                                 /*node_id=*/"UNUSED", proxy_status_config_),
      AllOf(
          HasSubstr("details=\"some_response_code_details; some_connection_termination_details\""),
          Not(HasSubstr("DI"))));
}

TEST_F(ProxyStatusTest, ToStringNoServerName) {
  proxy_status_config_.set_proxy_name(HttpConnectionManager::ProxyStatusConfig::ENVOY_LITERAL);
  EXPECT_THAT(ProxyStatusUtils::toString(stream_info_, ProxyStatusError::ProxyConfigurationError,
                                         /*node_id=*/"UNUSED", proxy_status_config_),
              AllOf(HasSubstr("envoy"), Not(HasSubstr("UNUSED"))));
}

TEST_F(ProxyStatusTest, ToStringServerName) {
  proxy_status_config_.set_proxy_name(HttpConnectionManager::ProxyStatusConfig::NODE_ID);
  EXPECT_THAT(ProxyStatusUtils::toString(stream_info_, ProxyStatusError::ProxyConfigurationError,
                                         /*node_id=*/"foo", proxy_status_config_),
              AllOf(HasSubstr("foo"), Not(HasSubstr("envoy"))));
}

} // namespace
} // namespace StreamInfo
} // namespace Envoy
