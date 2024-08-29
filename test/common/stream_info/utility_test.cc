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

REGISTER_CUSTOM_RESPONSE_FLAG(CF, CustomFlag);
REGISTER_CUSTOM_RESPONSE_FLAG(CF2, CustomFlag2);

TEST(ResponseFlagUtilsTest, toShortStringConversion) {
  for (const auto& [short_string, long_string, flag] : ResponseFlagUtils::responseFlagsVec()) {
    NiceMock<MockStreamInfo> stream_info;
    stream_info.response_flags_.clear();
    stream_info.response_flags_.push_back(flag);
    EXPECT_EQ(short_string, ResponseFlagUtils::toShortString(stream_info));
    EXPECT_EQ(long_string, ResponseFlagUtils::toString(stream_info));
  }

  // Custom flag.
  {
    EXPECT_EQ(ResponseFlagUtils::responseFlagsVec().size(),
              ResponseFlagUtils::responseFlagsMap().size());

    // There are two custom flags are registered.
    EXPECT_EQ(ResponseFlagUtils::CORE_RESPONSE_FLAGS.size() + 2,
              ResponseFlagUtils::responseFlagsMap().size());

    EXPECT_EQ(CUSTOM_RESPONSE_FLAG(CF).value(), CoreResponseFlag::LastFlag + 1);
    EXPECT_EQ(CUSTOM_RESPONSE_FLAG(CF2).value(), CoreResponseFlag::LastFlag + 2);

    // Verify the custom flags are registered correctly.
    EXPECT_EQ(CUSTOM_RESPONSE_FLAG(CF), ResponseFlagUtils::responseFlagsMap().at("CF").flag_);
    EXPECT_EQ(CUSTOM_RESPONSE_FLAG(CF2), ResponseFlagUtils::responseFlagsMap().at("CF2").flag_);
    EXPECT_EQ(ResponseFlagUtils::responseFlagsVec()[CUSTOM_RESPONSE_FLAG(CF).value()].short_string_,
              "CF");
    EXPECT_EQ(ResponseFlagUtils::responseFlagsVec()[CUSTOM_RESPONSE_FLAG(CF).value()].long_string_,
              "CustomFlag");
    EXPECT_EQ(
        ResponseFlagUtils::responseFlagsVec()[CUSTOM_RESPONSE_FLAG(CF2).value()].short_string_,
        "CF2");
    EXPECT_EQ(ResponseFlagUtils::responseFlagsVec()[CUSTOM_RESPONSE_FLAG(CF2).value()].long_string_,
              "CustomFlag2");

    // Verify the custom flag could work as expected.

    NiceMock<MockStreamInfo> stream_info;
    stream_info.response_flags_.clear();

    stream_info.setResponseFlag(CUSTOM_RESPONSE_FLAG(CF));
    EXPECT_TRUE(stream_info.hasAnyResponseFlag());
    EXPECT_TRUE(stream_info.hasResponseFlag(CUSTOM_RESPONSE_FLAG(CF)));

    EXPECT_EQ("CF", ResponseFlagUtils::toShortString(stream_info));
    EXPECT_EQ("CustomFlag", ResponseFlagUtils::toString(stream_info));

    stream_info.response_flags_.push_back(CUSTOM_RESPONSE_FLAG(CF2));

    EXPECT_TRUE(stream_info.hasAnyResponseFlag());
    EXPECT_TRUE(stream_info.hasResponseFlag(CUSTOM_RESPONSE_FLAG(CF)));
    EXPECT_TRUE(stream_info.hasResponseFlag(CUSTOM_RESPONSE_FLAG(CF2)));

    EXPECT_EQ("CF,CF2", ResponseFlagUtils::toShortString(stream_info));
    EXPECT_EQ("CustomFlag,CustomFlag2", ResponseFlagUtils::toString(stream_info));
  }

  // No flag is set.
  {
    NiceMock<MockStreamInfo> stream_info;
    EXPECT_EQ("-", ResponseFlagUtils::toShortString(stream_info));
    EXPECT_EQ("-", ResponseFlagUtils::toString(stream_info));
  }

  // Test combinations.
  // These are not real use cases, but are used to cover multiple response flags case.
  {
    NiceMock<MockStreamInfo> stream_info;
    stream_info.response_flags_.clear();
    stream_info.response_flags_.push_back(CoreResponseFlag::UpstreamRequestTimeout);
    stream_info.response_flags_.push_back(CoreResponseFlag::DelayInjected);
    stream_info.response_flags_.push_back(CoreResponseFlag::FaultInjected);

    EXPECT_EQ("UT,DI,FI", ResponseFlagUtils::toShortString(stream_info));
    EXPECT_EQ("UpstreamRequestTimeout,DelayInjected,FaultInjected",
              ResponseFlagUtils::toString(stream_info));
  }
}

TEST(ResponseFlagsUtilsTest, toResponseFlagConversion) {
  EXPECT_FALSE(ResponseFlagUtils::toResponseFlag("NonExistentFlag").has_value());

  for (const auto& [short_string, _, flag] : ResponseFlagUtils::CORE_RESPONSE_FLAGS) {
    auto response_flag = ResponseFlagUtils::toResponseFlag(short_string);
    EXPECT_TRUE(response_flag.has_value());
    EXPECT_EQ(flag, response_flag.value());
  }
}

TEST(UtilityTest, formatDownstreamAddressNoPort) {
  EXPECT_EQ("1.2.3.4",
            Utility::formatDownstreamAddressNoPort(Network::Address::Ipv4Instance("1.2.3.4")));
  EXPECT_EQ("/hello", Utility::formatDownstreamAddressNoPort(
                          **Network::Address::PipeInstance::create("/hello")));
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

    stream_info_.response_flags_.push_back(CoreResponseFlag::DelayInjected);
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
      {{"envoy.reloadable_features.proxy_status_mapping_more_core_response_flags", "false"}});
  for (const auto& [response_flag, proxy_status_error] :
       std::vector<std::pair<ResponseFlag, ProxyStatusError>>{
           {CoreResponseFlag::FailedLocalHealthCheck, ProxyStatusError::DestinationUnavailable},
           {CoreResponseFlag::NoHealthyUpstream, ProxyStatusError::DestinationUnavailable},
           {CoreResponseFlag::UpstreamRequestTimeout, ProxyStatusError::HttpResponseTimeout},
           {CoreResponseFlag::LocalReset, ProxyStatusError::ConnectionTimeout},
           {CoreResponseFlag::UpstreamRemoteReset, ProxyStatusError::ConnectionTerminated},
           {CoreResponseFlag::UpstreamConnectionFailure, ProxyStatusError::ConnectionRefused},
           {CoreResponseFlag::UpstreamConnectionTermination,
            ProxyStatusError::ConnectionTerminated},
           {CoreResponseFlag::UpstreamOverflow, ProxyStatusError::ConnectionLimitReached},
           {CoreResponseFlag::NoRouteFound, ProxyStatusError::DestinationNotFound},
           {CoreResponseFlag::RateLimited, ProxyStatusError::ConnectionLimitReached},
           {CoreResponseFlag::RateLimitServiceError, ProxyStatusError::ConnectionLimitReached},
           {CoreResponseFlag::UpstreamRetryLimitExceeded, ProxyStatusError::DestinationUnavailable},
           {CoreResponseFlag::StreamIdleTimeout, ProxyStatusError::HttpResponseTimeout},
           {CoreResponseFlag::InvalidEnvoyRequestHeaders, ProxyStatusError::HttpRequestError},
           {CoreResponseFlag::DownstreamProtocolError, ProxyStatusError::HttpRequestError},
           {CoreResponseFlag::UpstreamMaxStreamDurationReached,
            ProxyStatusError::HttpResponseTimeout},
           {CoreResponseFlag::NoFilterConfigFound, ProxyStatusError::ProxyConfigurationError},
           {CoreResponseFlag::UpstreamProtocolError, ProxyStatusError::HttpProtocolError},
           {CoreResponseFlag::NoClusterFound, ProxyStatusError::DestinationUnavailable},
           {CoreResponseFlag::DnsResolutionFailed, ProxyStatusError::DnsError}}) {
    NiceMock<MockStreamInfo> stream_info;
    ON_CALL(stream_info, hasResponseFlag(response_flag)).WillByDefault(Return(true));
    EXPECT_THAT(ProxyStatusUtils::fromStreamInfo(stream_info), proxy_status_error);
  }
}

TEST(ProxyStatusFromStreamInfo, TestNewAll) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.proxy_status_mapping_more_core_response_flags", "true"}});
  for (const auto& [response_flag, proxy_status_error] :
       std::vector<std::pair<ResponseFlag, ProxyStatusError>>{
           {CoreResponseFlag::FailedLocalHealthCheck, ProxyStatusError::DestinationUnavailable},
           {CoreResponseFlag::NoHealthyUpstream, ProxyStatusError::DestinationUnavailable},
           {CoreResponseFlag::UpstreamRequestTimeout, ProxyStatusError::HttpResponseTimeout},
           {CoreResponseFlag::DurationTimeout, ProxyStatusError::ConnectionTimeout},
           {CoreResponseFlag::LocalReset, ProxyStatusError::ConnectionTimeout},
           {CoreResponseFlag::UpstreamRemoteReset, ProxyStatusError::ConnectionTerminated},
           {CoreResponseFlag::UpstreamConnectionFailure, ProxyStatusError::ConnectionRefused},
           {CoreResponseFlag::UnauthorizedExternalService, ProxyStatusError::ConnectionRefused},
           {CoreResponseFlag::UpstreamConnectionTermination,
            ProxyStatusError::ConnectionTerminated},
           {CoreResponseFlag::DownstreamConnectionTermination,
            ProxyStatusError::ConnectionTerminated},
           {CoreResponseFlag::DownstreamRemoteReset, ProxyStatusError::ConnectionTerminated},
           {CoreResponseFlag::OverloadManager, ProxyStatusError::ConnectionLimitReached},
           {CoreResponseFlag::DropOverLoad, ProxyStatusError::ConnectionLimitReached},
           {CoreResponseFlag::FaultInjected, ProxyStatusError::HttpRequestError},
           {CoreResponseFlag::UpstreamOverflow, ProxyStatusError::ConnectionLimitReached},
           {CoreResponseFlag::NoRouteFound, ProxyStatusError::DestinationNotFound},
           {CoreResponseFlag::RateLimited, ProxyStatusError::ConnectionLimitReached},
           {CoreResponseFlag::RateLimitServiceError, ProxyStatusError::ConnectionLimitReached},
           {CoreResponseFlag::UpstreamRetryLimitExceeded, ProxyStatusError::DestinationUnavailable},
           {CoreResponseFlag::StreamIdleTimeout, ProxyStatusError::HttpResponseTimeout},
           {CoreResponseFlag::InvalidEnvoyRequestHeaders, ProxyStatusError::HttpRequestError},
           {CoreResponseFlag::DownstreamProtocolError, ProxyStatusError::HttpRequestError},
           {CoreResponseFlag::UpstreamMaxStreamDurationReached,
            ProxyStatusError::HttpResponseTimeout},
           {CoreResponseFlag::NoFilterConfigFound, ProxyStatusError::ProxyConfigurationError},
           {CoreResponseFlag::UpstreamProtocolError, ProxyStatusError::HttpProtocolError},
           {CoreResponseFlag::NoClusterFound, ProxyStatusError::DestinationUnavailable},
           {CoreResponseFlag::DnsResolutionFailed, ProxyStatusError::DnsError}}) {
    NiceMock<MockStreamInfo> stream_info;
    ON_CALL(stream_info, hasResponseFlag(response_flag)).WillByDefault(Return(true));
    EXPECT_THAT(ProxyStatusUtils::fromStreamInfo(stream_info), proxy_status_error);
  }
}

} // namespace
} // namespace StreamInfo
} // namespace Envoy
