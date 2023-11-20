#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/network/address_impl.h"
#include "source/common/stream_info/utility.h"

#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/test_runtime.h"

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
  for (const auto& [flag_strings, flag_enum] : ResponseFlagUtils::ALL_RESPONSE_STRINGS_FLAGS) {
    NiceMock<MockStreamInfo> stream_info;
    ON_CALL(stream_info, hasResponseFlag(flag_enum)).WillByDefault(Return(true));
    EXPECT_EQ(flag_strings.short_string_, ResponseFlagUtils::toShortString(stream_info));
    EXPECT_EQ(flag_strings.long_string_, ResponseFlagUtils::toString(stream_info));
  }

  // No flag is set.
  {
    NiceMock<MockStreamInfo> stream_info;
    ON_CALL(stream_info, hasResponseFlag(_)).WillByDefault(Return(false));
    EXPECT_EQ("-", ResponseFlagUtils::toShortString(stream_info));
    EXPECT_EQ("-", ResponseFlagUtils::toString(stream_info));
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
    EXPECT_EQ("UpstreamRequestTimeout,DelayInjected,FaultInjected",
              ResponseFlagUtils::toString(stream_info));
  }
}

TEST(ResponseFlagsUtilsTest, toResponseFlagConversion) {
  EXPECT_FALSE(ResponseFlagUtils::toResponseFlag("NonExistentFlag").has_value());

  for (const auto& [flag_strings, flag_enum] : ResponseFlagUtils::ALL_RESPONSE_STRINGS_FLAGS) {
    absl::optional<ResponseFlag> response_flag =
        ResponseFlagUtils::toResponseFlag(flag_strings.short_string_);
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

TEST(UtilityTest, formatDownstreamAddressJustPort) {
  EXPECT_EQ("0",
            Utility::formatDownstreamAddressJustPort(Network::Address::Ipv4Instance("1.2.3.4")));
  EXPECT_EQ("8080", Utility::formatDownstreamAddressJustPort(
                        Network::Address::Ipv4Instance("1.2.3.4", 8080)));
}

TEST(UtilityTest, extractDownstreamAddressJustPort) {

  EXPECT_EQ(0,
            *Utility::extractDownstreamAddressJustPort(Network::Address::Ipv4Instance("1.2.3.4")));
  EXPECT_EQ(8080, *Utility::extractDownstreamAddressJustPort(
                      Network::Address::Ipv4Instance("1.2.3.4", 8080)));
}

class ProxyStatusTest : public ::testing::Test {
protected:
  void SetUp() override {
    proxy_status_config_.set_remove_details(false);

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
  EXPECT_THAT(ProxyStatusUtils::makeProxyStatusHeader(
                  stream_info_, ProxyStatusError::ProxyConfigurationError,
                  /*proxy_name=*/"UNUSED", proxy_status_config_),
              Not(HasSubstr("details=")));
}

TEST_F(ProxyStatusTest, ToStringNoDetails) {
  proxy_status_config_.set_remove_details(true);
  proxy_status_config_.set_remove_connection_termination_details(false);
  proxy_status_config_.set_remove_response_flags(false);
  stream_info_.response_code_details_ = "some_response_code_details";
  stream_info_.connection_termination_details_ = "some_connection_termination_details";
  EXPECT_THAT(ProxyStatusUtils::makeProxyStatusHeader(
                  stream_info_, ProxyStatusError::ProxyConfigurationError,
                  /*proxy_name=*/"UNUSED", proxy_status_config_),
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
      ProxyStatusUtils::makeProxyStatusHeader(stream_info_,
                                              ProxyStatusError::ProxyConfigurationError,
                                              /*proxy_name=*/"UNUSED", proxy_status_config_),
      HasSubstr("details=\"some_response_code_details; some_connection_termination_details; DI\""));
}

TEST_F(ProxyStatusTest, ToStringWithAllFieldsVerifyStringEscape) {
  proxy_status_config_.set_remove_details(false);
  proxy_status_config_.set_remove_connection_termination_details(false);
  proxy_status_config_.set_remove_response_flags(false);
  stream_info_.response_code_details_ = "some \"response\" code details";
  stream_info_.connection_termination_details_ = "some \"connection\" termination details";
  EXPECT_THAT(ProxyStatusUtils::makeProxyStatusHeader(
                  stream_info_, ProxyStatusError::ProxyConfigurationError,
                  /*proxy_name=*/"UNUSED", proxy_status_config_),
              HasSubstr("details=\"some \\\"response\\\" code details; some "
                        "\\\"connection\\\" termination details; DI\""));
}

TEST_F(ProxyStatusTest, ToStringNoConnectionTerminationDetails) {
  proxy_status_config_.set_remove_details(false);
  proxy_status_config_.set_remove_connection_termination_details(true);
  proxy_status_config_.set_remove_response_flags(false);
  stream_info_.response_code_details_ = "some_response_code_details";
  stream_info_.connection_termination_details_ = "some_connection_termination_details";
  EXPECT_THAT(ProxyStatusUtils::makeProxyStatusHeader(
                  stream_info_, ProxyStatusError::ProxyConfigurationError,
                  /*proxy_name=*/"UNUSED", proxy_status_config_),
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
      ProxyStatusUtils::makeProxyStatusHeader(stream_info_,
                                              ProxyStatusError::ProxyConfigurationError,
                                              /*proxy_name=*/"UNUSED", proxy_status_config_),
      HasSubstr("details=\"some_response_code_details; DI\"")); // So they shouldn't be printed.
}

TEST_F(ProxyStatusTest, ToStringNoResponseFlags) {
  proxy_status_config_.set_remove_details(false);
  proxy_status_config_.set_remove_connection_termination_details(false);
  proxy_status_config_.set_remove_response_flags(true);
  stream_info_.response_code_details_ = "some_response_code_details";
  stream_info_.connection_termination_details_ = "some_connection_termination_details";
  EXPECT_THAT(
      ProxyStatusUtils::makeProxyStatusHeader(stream_info_,
                                              ProxyStatusError::ProxyConfigurationError,
                                              /*proxy_name=*/"UNUSED", proxy_status_config_),
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
      ProxyStatusUtils::makeProxyStatusHeader(stream_info_,
                                              ProxyStatusError::ProxyConfigurationError,
                                              /*proxy_name=*/"UNUSED", proxy_status_config_),
      AllOf(
          HasSubstr("details=\"some_response_code_details; some_connection_termination_details\""),
          Not(HasSubstr("DI"))));
}

TEST_F(ProxyStatusTest, ToStringNoConfig) {
  EXPECT_THAT(
      ProxyStatusUtils::makeProxyName(/*node_id=*/"UNUSED", /*server_name=*/"envoy", nullptr),
      AllOf(HasSubstr("envoy"), Not(HasSubstr("UNUSED"))));
}

TEST_F(ProxyStatusTest, ToStringNoServerName) {
  EXPECT_THAT(ProxyStatusUtils::makeProxyName(/*node_id=*/"UNUSED", /*server_name=*/"envoy",
                                              &proxy_status_config_),
              AllOf(HasSubstr("envoy"), Not(HasSubstr("UNUSED"))));
}

TEST_F(ProxyStatusTest, ToStringServerName) {
  proxy_status_config_.set_use_node_id(true);
  EXPECT_THAT(ProxyStatusUtils::makeProxyName(/*node_id=*/"foo", /*server_name=*/"envoy",
                                              &proxy_status_config_),
              AllOf(HasSubstr("foo"), Not(HasSubstr("envoy"))));
}

TEST_F(ProxyStatusTest, ToStringLiteral) {
  proxy_status_config_.set_literal_proxy_name("foo_bar_baz");
  EXPECT_THAT(ProxyStatusUtils::makeProxyName(/*node_id=*/"foo", /*server_name=*/"envoy",
                                              &proxy_status_config_),
              AllOf(HasSubstr("foo_bar_baz"), Not(HasSubstr("envoy")), Not(HasSubstr("UNUSED"))));
}

TEST(ProxyStatusRecommendedHttpStatusCode, TestAll) {
  for (const auto& [proxy_status_error, http_code] :
       std::vector<std::pair<ProxyStatusError, absl::optional<Http::Code>>>{
           {ProxyStatusError::DnsTimeout, Http::Code::GatewayTimeout},
           {ProxyStatusError::DnsTimeout, Http::Code::GatewayTimeout},
           {ProxyStatusError::DnsTimeout, Http::Code::GatewayTimeout},
           {ProxyStatusError::ConnectionTimeout, Http::Code::GatewayTimeout},
           {ProxyStatusError::ConnectionReadTimeout, Http::Code::GatewayTimeout},
           {ProxyStatusError::ConnectionWriteTimeout, Http::Code::GatewayTimeout},
           {ProxyStatusError::HttpResponseTimeout, Http::Code::GatewayTimeout},
           {ProxyStatusError::DnsError, Http::Code::BadGateway},
           {ProxyStatusError::DestinationIpProhibited, Http::Code::BadGateway},
           {ProxyStatusError::DestinationIpUnroutable, Http::Code::BadGateway},
           {ProxyStatusError::ConnectionRefused, Http::Code::BadGateway},
           {ProxyStatusError::ConnectionTerminated, Http::Code::BadGateway},
           {ProxyStatusError::TlsProtocolError, Http::Code::BadGateway},
           {ProxyStatusError::TlsCertificateError, Http::Code::BadGateway},
           {ProxyStatusError::TlsAlertReceived, Http::Code::BadGateway},
           {ProxyStatusError::HttpResponseIncomplete, Http::Code::BadGateway},
           {ProxyStatusError::HttpResponseHeaderSectionSize, Http::Code::BadGateway},
           {ProxyStatusError::HttpResponseHeaderSize, Http::Code::BadGateway},
           {ProxyStatusError::HttpResponseBodySize, Http::Code::BadGateway},
           {ProxyStatusError::HttpResponseTrailerSectionSize, Http::Code::BadGateway},
           {ProxyStatusError::HttpResponseTrailerSize, Http::Code::BadGateway},
           {ProxyStatusError::HttpResponseTransferCoding, Http::Code::BadGateway},
           {ProxyStatusError::HttpResponseContentCoding, Http::Code::BadGateway},
           {ProxyStatusError::HttpUpgradeFailed, Http::Code::BadGateway},
           {ProxyStatusError::HttpProtocolError, Http::Code::BadGateway},
           {ProxyStatusError::ProxyLoopDetected, Http::Code::BadGateway},
           {ProxyStatusError::DestinationNotFound, Http::Code::InternalServerError},
           {ProxyStatusError::ProxyInternalError, Http::Code::InternalServerError},
           {ProxyStatusError::ProxyConfigurationError, Http::Code::InternalServerError},
           {ProxyStatusError::DestinationUnavailable, Http::Code::ServiceUnavailable},
           {ProxyStatusError::ConnectionLimitReached, Http::Code::ServiceUnavailable},
           {ProxyStatusError::HttpRequestDenied, Http::Code::Forbidden},
           {ProxyStatusError::ProxyInternalResponse, absl::nullopt},
           {ProxyStatusError::HttpRequestError, absl::nullopt},
       }) {
    EXPECT_THAT(ProxyStatusUtils::recommendedHttpStatusCode(proxy_status_error), http_code);
  }
}

TEST(ProxyStatusErrorToString, TestAll) {
  for (const auto& [proxy_status_error, error_string] :
       std::vector<std::pair<ProxyStatusError, std::string>>{
           {ProxyStatusError::DnsTimeout, "dns_timeout"},
           {ProxyStatusError::DnsError, "dns_error"},
           {ProxyStatusError::DestinationNotFound, "destination_not_found"},
           {ProxyStatusError::DestinationUnavailable, "destination_unavailable"},
           {ProxyStatusError::DestinationIpProhibited, "destination_ip_prohibited"},
           {ProxyStatusError::DestinationIpUnroutable, "destination_ip_unroutable"},
           {ProxyStatusError::ConnectionRefused, "connection_refused"},
           {ProxyStatusError::ConnectionTerminated, "connection_terminated"},
           {ProxyStatusError::ConnectionTimeout, "connection_timeout"},
           {ProxyStatusError::ConnectionReadTimeout, "connection_read_timeout"},
           {ProxyStatusError::ConnectionWriteTimeout, "connection_write_timeout"},
           {ProxyStatusError::ConnectionLimitReached, "connection_limit_reached"},
           {ProxyStatusError::TlsProtocolError, "tls_protocol_error"},
           {ProxyStatusError::TlsCertificateError, "tls_certificate_error"},
           {ProxyStatusError::TlsAlertReceived, "tls_alert_received"},
           {ProxyStatusError::HttpRequestError, "http_request_error"},
           {ProxyStatusError::HttpRequestDenied, "http_request_denied"},
           {ProxyStatusError::HttpResponseIncomplete, "http_response_incomplete"},
           {ProxyStatusError::HttpResponseHeaderSectionSize, "http_response_header_section_size"},
           {ProxyStatusError::HttpResponseHeaderSize, "http_response_header_size"},
           {ProxyStatusError::HttpResponseBodySize, "http_response_body_size"},
           {ProxyStatusError::HttpResponseTrailerSectionSize, "http_response_trailer_section_size"},
           {ProxyStatusError::HttpResponseTrailerSize, "http_response_trailer_size"},
           {ProxyStatusError::HttpResponseTransferCoding, "http_response_transfer_coding"},
           {ProxyStatusError::HttpResponseContentCoding, "http_response_content_coding"},
           {ProxyStatusError::HttpResponseTimeout, "http_response_timeout"},
           {ProxyStatusError::HttpUpgradeFailed, "http_upgrade_failed"},
           {ProxyStatusError::HttpProtocolError, "http_protocol_error"},
           {ProxyStatusError::ProxyInternalResponse, "proxy_internal_response"},
           {ProxyStatusError::ProxyInternalError, "proxy_internal_error"},
           {ProxyStatusError::ProxyConfigurationError, "proxy_configuration_error"},
           {ProxyStatusError::ProxyLoopDetected, "proxy_loop_detected"},
       }) {
    EXPECT_THAT(ProxyStatusUtils::proxyStatusErrorToString(proxy_status_error), error_string);
  }
}

TEST(ProxyStatusFromStreamInfo, TestAll) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.proxy_status_upstream_request_timeout", "true"}});
  for (const auto& [response_flag, proxy_status_error] :
       std::vector<std::pair<ResponseFlag, ProxyStatusError>>{
           {ResponseFlag::FailedLocalHealthCheck, ProxyStatusError::DestinationUnavailable},
           {ResponseFlag::NoHealthyUpstream, ProxyStatusError::DestinationUnavailable},
           {ResponseFlag::UpstreamRequestTimeout, ProxyStatusError::HttpResponseTimeout},
           {ResponseFlag::LocalReset, ProxyStatusError::ConnectionTimeout},
           {ResponseFlag::UpstreamRemoteReset, ProxyStatusError::ConnectionTerminated},
           {ResponseFlag::UpstreamConnectionFailure, ProxyStatusError::ConnectionRefused},
           {ResponseFlag::UpstreamConnectionTermination, ProxyStatusError::ConnectionTerminated},
           {ResponseFlag::UpstreamOverflow, ProxyStatusError::ConnectionLimitReached},
           {ResponseFlag::NoRouteFound, ProxyStatusError::DestinationNotFound},
           {ResponseFlag::RateLimited, ProxyStatusError::ConnectionLimitReached},
           {ResponseFlag::RateLimitServiceError, ProxyStatusError::ConnectionLimitReached},
           {ResponseFlag::UpstreamRetryLimitExceeded, ProxyStatusError::DestinationUnavailable},
           {ResponseFlag::StreamIdleTimeout, ProxyStatusError::HttpResponseTimeout},
           {ResponseFlag::InvalidEnvoyRequestHeaders, ProxyStatusError::HttpRequestError},
           {ResponseFlag::DownstreamProtocolError, ProxyStatusError::HttpRequestError},
           {ResponseFlag::UpstreamMaxStreamDurationReached, ProxyStatusError::HttpResponseTimeout},
           {ResponseFlag::NoFilterConfigFound, ProxyStatusError::ProxyConfigurationError},
           {ResponseFlag::UpstreamProtocolError, ProxyStatusError::HttpProtocolError},
           {ResponseFlag::NoClusterFound, ProxyStatusError::DestinationUnavailable},
           {ResponseFlag::DnsResolutionFailed, ProxyStatusError::DnsError}}) {
    NiceMock<MockStreamInfo> stream_info;
    ON_CALL(stream_info, hasResponseFlag(response_flag)).WillByDefault(Return(true));
    EXPECT_THAT(ProxyStatusUtils::fromStreamInfo(stream_info), proxy_status_error);
  }
}

TEST(ProxyStatusFromStreamInfo, TestUpstreamRequestTimeout) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.proxy_status_upstream_request_timeout", "false"}});
  NiceMock<MockStreamInfo> stream_info;
  ON_CALL(stream_info, hasResponseFlag(ResponseFlag::UpstreamRequestTimeout))
      .WillByDefault(Return(true));
  EXPECT_THAT(ProxyStatusUtils::fromStreamInfo(stream_info), ProxyStatusError::ConnectionTimeout);
}

} // namespace
} // namespace StreamInfo
} // namespace Envoy
