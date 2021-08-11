#include <string>

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/http/request_id_extension.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/common/random_generator.h"
#include "source/common/http/conn_manager_utility.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/common/runtime/runtime_impl.h"
#include "source/extensions/request_id/uuid/config.h"

#include "test/common/http/custom_header_extension.h"
#include "test/common/http/xff_extension.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::An;
using testing::Matcher;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Http {

class MockRequestIDExtension : public RequestIDExtension {
public:
  explicit MockRequestIDExtension(Random::RandomGenerator& random)
      : real_(Extensions::RequestId::UUIDRequestIDExtension::defaultInstance(random)) {
    ON_CALL(*this, set(_, _))
        .WillByDefault([this](Http::RequestHeaderMap& request_headers, bool force) {
          return real_->set(request_headers, force);
        });
    ON_CALL(*this, setInResponse(_, _))
        .WillByDefault([this](Http::ResponseHeaderMap& response_headers,
                              const Http::RequestHeaderMap& request_headers) {
          return real_->setInResponse(response_headers, request_headers);
        });
    ON_CALL(*this, toInteger(_))
        .WillByDefault([this](const Http::RequestHeaderMap& request_headers) {
          return real_->toInteger(request_headers);
        });
    ON_CALL(*this, getTraceReason(_))
        .WillByDefault([this](const Http::RequestHeaderMap& request_headers) {
          return real_->getTraceReason(request_headers);
        });
    ON_CALL(*this, setTraceReason(_, _))
        .WillByDefault(
            [this](Http::RequestHeaderMap& request_headers, Tracing::Reason trace_status) {
              real_->setTraceReason(request_headers, trace_status);
            });
    ON_CALL(*this, useRequestIdForTraceSampling()).WillByDefault(Return(true));
  }

  MOCK_METHOD(void, set, (Http::RequestHeaderMap&, bool));
  MOCK_METHOD(void, setInResponse, (Http::ResponseHeaderMap&, const Http::RequestHeaderMap&));
  MOCK_METHOD(absl::optional<uint64_t>, toInteger, (const Http::RequestHeaderMap&), (const));
  MOCK_METHOD(Tracing::Reason, getTraceReason, (const Http::RequestHeaderMap&));
  MOCK_METHOD(void, setTraceReason, (Http::RequestHeaderMap&, Tracing::Reason));
  MOCK_METHOD(bool, useRequestIdForTraceSampling, (), (const));

private:
  RequestIDExtensionSharedPtr real_;
};

class MockInternalAddressConfig : public Http::InternalAddressConfig {
public:
  MOCK_METHOD(bool, isInternalAddress, (const Network::Address::Instance&), (const));
};

const Http::LowerCaseString& traceStatusHeader() {
  static Http::LowerCaseString header("x-trace-status");
  return header;
}

class ConnectionManagerUtilityTest : public testing::Test {
public:
  ConnectionManagerUtilityTest()
      : request_id_extension_(std::make_shared<NiceMock<MockRequestIDExtension>>(random_)),
        request_id_extension_to_return_(request_id_extension_),
        local_reply_(LocalReply::Factory::createDefault()) {
    ON_CALL(config_, userAgent()).WillByDefault(ReturnRef(user_agent_));

    envoy::type::v3::FractionalPercent percent1;
    percent1.set_numerator(100);
    envoy::type::v3::FractionalPercent percent2;
    percent2.set_numerator(10000);
    percent2.set_denominator(envoy::type::v3::FractionalPercent::TEN_THOUSAND);
    tracing_config_ = {
        Tracing::OperationName::Ingress, {}, percent1, percent2, percent1, false, 256};
    ON_CALL(config_, tracingConfig()).WillByDefault(Return(&tracing_config_));
    ON_CALL(config_, localReply()).WillByDefault(ReturnRef(*local_reply_));

    ON_CALL(config_, via()).WillByDefault(ReturnRef(via_));
    ON_CALL(config_, requestIDExtension())
        .WillByDefault(ReturnRef(request_id_extension_to_return_));
    ON_CALL(config_, pathWithEscapedSlashesAction())
        .WillByDefault(Return(envoy::extensions::filters::network::http_connection_manager::v3::
                                  HttpConnectionManager::KEEP_UNCHANGED));

    detection_extensions_.push_back(getXFFExtension(0));
    ON_CALL(config_, originalIpDetectionExtensions())
        .WillByDefault(ReturnRef(detection_extensions_));
  }

  struct MutateRequestRet {
    MutateRequestRet() = default;
    MutateRequestRet(const std::string& downstream_address, bool internal,
                     Tracing::Reason trace_reason)
        : downstream_address_(downstream_address), internal_(internal),
          trace_reason_(trace_reason) {}
    bool operator==(const MutateRequestRet& rhs) const {
      return downstream_address_ == rhs.downstream_address_ && internal_ == rhs.internal_ &&
             trace_reason_ == rhs.trace_reason_;
    }

    std::string downstream_address_;
    bool internal_;
    Tracing::Reason trace_reason_;
    absl::optional<OriginalIPRejectRequestOptions> reject_request_{absl::nullopt};
  };

  // This is a convenience method used to call mutateRequestHeaders(). It is done in this
  // convoluted way to force tests to check both the final downstream address as well as whether
  // the request is internal/external, given the importance of these two pieces of data.
  MutateRequestRet callMutateRequestHeaders(RequestHeaderMap& headers, Protocol) {
    MutateRequestRet ret;
    const auto result = ConnectionManagerUtility::mutateRequestHeaders(
        headers, connection_, config_, route_config_, local_info_);
    ret.downstream_address_ = result.final_remote_address->asString();
    ret.reject_request_ = result.reject_request;
    ret.trace_reason_ =
        ConnectionManagerUtility::mutateTracingRequestHeader(headers, runtime_, config_, &route_);
    ret.internal_ = HeaderUtility::isEnvoyInternalRequest(headers);
    return ret;
  }

  NiceMock<Network::MockConnection> connection_;
  NiceMock<Random::MockRandomGenerator> random_;
  const std::shared_ptr<MockRequestIDExtension> request_id_extension_;
  const std::shared_ptr<RequestIDExtension> request_id_extension_to_return_;
  std::vector<Http::OriginalIPDetectionSharedPtr> detection_extensions_{};
  NiceMock<MockConnectionManagerConfig> config_;
  NiceMock<Router::MockConfig> route_config_;
  NiceMock<Router::MockRoute> route_;
  absl::optional<std::string> user_agent_;
  NiceMock<Runtime::MockLoader> runtime_;
  Http::TracingConnectionManagerConfig tracing_config_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  LocalReply::LocalReplyPtr local_reply_;
  std::string canary_node_{"canary"};
  std::string empty_node_;
  std::string via_;
};

// Tests for ConnectionManagerUtility::determineNextProtocol.
TEST_F(ConnectionManagerUtilityTest, DetermineNextProtocol) {
  {
    Network::MockConnection connection;
    EXPECT_CALL(connection, nextProtocol()).WillRepeatedly(Return("hello"));
    Buffer::OwnedImpl data("");
    EXPECT_EQ("hello", ConnectionManagerUtility::determineNextProtocol(connection, data));
  }

  {
    Network::MockConnection connection;
    EXPECT_CALL(connection, nextProtocol()).WillRepeatedly(Return(""));
    Buffer::OwnedImpl data("");
    EXPECT_EQ("", ConnectionManagerUtility::determineNextProtocol(connection, data));
  }

  {
    Network::MockConnection connection;
    EXPECT_CALL(connection, nextProtocol()).WillRepeatedly(Return(""));
    Buffer::OwnedImpl data("GET / HTTP/1.1");
    EXPECT_EQ("", ConnectionManagerUtility::determineNextProtocol(connection, data));
  }

  {
    Network::MockConnection connection;
    EXPECT_CALL(connection, nextProtocol()).WillRepeatedly(Return(""));
    Buffer::OwnedImpl data("PRI * HTTP/2.0\r\n");
    EXPECT_EQ(Utility::AlpnNames::get().Http2,
              ConnectionManagerUtility::determineNextProtocol(connection, data));
  }

  {
    Network::MockConnection connection;
    EXPECT_CALL(connection, nextProtocol()).WillRepeatedly(Return(""));
    Buffer::OwnedImpl data("PRI * HTTP/2");
    EXPECT_EQ(Utility::AlpnNames::get().Http2,
              ConnectionManagerUtility::determineNextProtocol(connection, data));
  }

  {
    Network::MockConnection connection;
    EXPECT_CALL(connection, nextProtocol()).WillRepeatedly(Return(""));
    Buffer::OwnedImpl data("PRI * HTTP/");
    EXPECT_EQ("", ConnectionManagerUtility::determineNextProtocol(connection, data));
  }

  {
    Network::MockConnection connection;
    EXPECT_CALL(connection, nextProtocol()).WillRepeatedly(Return(""));
    Buffer::OwnedImpl data(" PRI * HTTP/2");
    EXPECT_EQ("", ConnectionManagerUtility::determineNextProtocol(connection, data));
  }
}

// Verify external request and XFF is set when we are using remote address and the address is
// external.
TEST_F(ConnectionManagerUtilityTest, UseRemoteAddressWhenNotLocalHostRemoteAddress) {
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("12.12.12.12"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  TestRequestHeaderMapImpl headers;

  EXPECT_EQ((MutateRequestRet{"12.12.12.12:0", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ(connection_.stream_info_.downstream_address_provider_->remoteAddress()
                ->ip()
                ->addressAsString(),
            headers.get_(Headers::get().ForwardedFor));
}

// Verify that we don't append XFF when skipXffAppend(), even if using remote
// address and where the address is external.
TEST_F(ConnectionManagerUtilityTest, SkipXffAppendUseRemoteAddress) {
  EXPECT_CALL(config_, skipXffAppend()).WillOnce(Return(true));
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("12.12.12.12"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  TestRequestHeaderMapImpl headers;

  EXPECT_EQ((MutateRequestRet{"12.12.12.12:0", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_FALSE(headers.has(Headers::get().ForwardedFor));
}

// Verify that we pass-thru XFF when skipXffAppend(), even if using remote
// address and where the address is external.
TEST_F(ConnectionManagerUtilityTest, SkipXffAppendPassThruUseRemoteAddress) {
  EXPECT_CALL(config_, skipXffAppend()).WillOnce(Return(true));
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("12.12.12.12"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  TestRequestHeaderMapImpl headers{{"x-forwarded-for", "198.51.100.1"}};

  EXPECT_EQ((MutateRequestRet{"12.12.12.12:0", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ("198.51.100.1", headers.getForwardedForValue());
}

TEST_F(ConnectionManagerUtilityTest, PreserveForwardedProtoWhenInternal) {
  TestScopedRuntime scoped_runtime;

  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  ON_CALL(config_, xffNumTrustedHops()).WillByDefault(Return(1));
  EXPECT_CALL(config_, skipXffAppend()).WillOnce(Return(true));
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("12.12.12.12"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  TestRequestHeaderMapImpl headers{{"x-forwarded-proto", "https"}};

  callMutateRequestHeaders(headers, Protocol::Http2);
  EXPECT_EQ("https", headers.getForwardedProtoValue());
  // Given :scheme was not set, it will be set to X-Forwarded-Proto
  EXPECT_EQ("https", headers.getSchemeValue());

  // Make sure if x-forwarded-proto changes it doesn't cause problems.
  headers.setForwardedProto("ftp");
  EXPECT_EQ("https", headers.getSchemeValue());
}

TEST_F(ConnectionManagerUtilityTest, OverwriteForwardedProtoWhenExternal) {
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  ON_CALL(config_, xffNumTrustedHops()).WillByDefault(Return(0));
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1"));
  TestRequestHeaderMapImpl headers{{"x-forwarded-proto", "https"}};
  Network::Address::Ipv4Instance local_address("10.3.2.1");
  ON_CALL(config_, localAddress()).WillByDefault(ReturnRef(local_address));

  callMutateRequestHeaders(headers, Protocol::Http2);
  EXPECT_EQ("http", headers.getForwardedProtoValue());
  // Given :scheme was not set, it will be set to X-Forwarded-Proto
  EXPECT_EQ("http", headers.getSchemeValue());
}

TEST_F(ConnectionManagerUtilityTest, PreserveForwardedProtoWhenInternalButSetScheme) {
  TestScopedRuntime scoped_runtime;

  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  ON_CALL(config_, xffNumTrustedHops()).WillByDefault(Return(1));
  EXPECT_CALL(config_, skipXffAppend()).WillOnce(Return(true));
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("12.12.12.12"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  TestRequestHeaderMapImpl headers{{"x-forwarded-proto", "foo"}};

  callMutateRequestHeaders(headers, Protocol::Http2);
  EXPECT_EQ("foo", headers.getForwardedProtoValue());
  // Given :scheme was not set, but X-Forwarded-Proto is not a valid scheme,
  // scheme will be set based on encryption level.
  EXPECT_EQ("http", headers.getSchemeValue());
}

TEST_F(ConnectionManagerUtilityTest, SchemeIsRespected) {
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  ON_CALL(config_, xffNumTrustedHops()).WillByDefault(Return(0));
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1"));
  TestRequestHeaderMapImpl headers{{"x-forwarded-proto", "https"}, {":scheme", "https"}};
  Network::Address::Ipv4Instance local_address("10.3.2.1");
  ON_CALL(config_, localAddress()).WillByDefault(ReturnRef(local_address));

  callMutateRequestHeaders(headers, Protocol::Http2);
  EXPECT_EQ("http", headers.getForwardedProtoValue());
  // Given :scheme was set, it will not be changed.
  EXPECT_EQ("https", headers.getSchemeValue());
}

TEST_F(ConnectionManagerUtilityTest, SchemeOverwrite) {
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  ON_CALL(config_, xffNumTrustedHops()).WillByDefault(Return(0));
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1"));
  TestRequestHeaderMapImpl headers{};
  Network::Address::Ipv4Instance local_address("10.3.2.1");
  ON_CALL(config_, localAddress()).WillByDefault(ReturnRef(local_address));

  // Scheme was present. Do not overwrite anything
  // Scheme and X-Forwarded-Proto will be overwritten.
  config_.scheme_ = "https";
  callMutateRequestHeaders(headers, Protocol::Http2);
  EXPECT_EQ("https", headers.getSchemeValue());
  EXPECT_EQ("https", headers.getForwardedProtoValue());
}

// Verify internal request and XFF is set when we are using remote address and the address is
// internal according to user configuration.
TEST_F(ConnectionManagerUtilityTest, UseRemoteAddressWhenUserConfiguredRemoteAddress) {
  auto config = std::make_unique<NiceMock<MockInternalAddressConfig>>();
  ON_CALL(*config, isInternalAddress).WillByDefault(Return(true));
  config_.internal_address_config_ = std::move(config);

  Network::Address::Ipv4Instance local_address("10.3.2.1");
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  ON_CALL(config_, localAddress()).WillByDefault(ReturnRef(local_address));

  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("12.12.12.12"));

  TestRequestHeaderMapImpl headers;
  EXPECT_EQ((MutateRequestRet{"12.12.12.12:0", true, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ("12.12.12.12", headers.get_(Headers::get().ForwardedFor));
}

// Verify internal request and XFF is set when we are using remote address the address is internal.
TEST_F(ConnectionManagerUtilityTest, UseRemoteAddressWhenLocalHostRemoteAddress) {
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1"));
  Network::Address::Ipv4Instance local_address("10.3.2.1");
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  ON_CALL(config_, localAddress()).WillByDefault(ReturnRef(local_address));
  TestRequestHeaderMapImpl headers;

  EXPECT_EQ((MutateRequestRet{"127.0.0.1:0", true, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ(local_address.ip()->addressAsString(), headers.get_(Headers::get().ForwardedFor));
}

// Verify that we trust Nth address from XFF when using remote address with xff_num_trusted_hops.
TEST_F(ConnectionManagerUtilityTest, UseRemoteAddressWithXFFTrustedHops) {
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("203.0.113.128"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  ON_CALL(config_, xffNumTrustedHops()).WillByDefault(Return(1));
  TestRequestHeaderMapImpl headers{{"x-forwarded-for", "198.51.100.1"}};
  EXPECT_EQ((MutateRequestRet{"198.51.100.1:0", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ(headers.EnvoyExternalAddress()->value(), "198.51.100.1");
}

// Verify that xff_num_trusted_hops works when not using remote address.
TEST_F(ConnectionManagerUtilityTest, UseXFFTrustedHopsWithoutRemoteAddress) {
  // Reconfigure XFF detection.
  detection_extensions_.clear();
  detection_extensions_.push_back(getXFFExtension(1));
  ON_CALL(config_, originalIpDetectionExtensions()).WillByDefault(ReturnRef(detection_extensions_));

  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(false));
  ON_CALL(config_, xffNumTrustedHops()).WillByDefault(Return(1));
  TestRequestHeaderMapImpl headers{{"x-forwarded-for", "198.51.100.2, 198.51.100.1"}};
  EXPECT_EQ((MutateRequestRet{"198.51.100.2:0", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ(headers.EnvoyExternalAddress(), nullptr);
}

// Verify we preserve hop by hop headers if configured to do so.
TEST_F(ConnectionManagerUtilityTest, PreserveHopByHop) {
  TestRequestHeaderMapImpl request_headers;
  TestResponseHeaderMapImpl response_headers{{"connection", "foo"},
                                             {"transfer-encoding", "foo"},
                                             {"upgrade", "eep"},
                                             {"keep-alive", "ads"},
                                             {"proxy-connection", "dsa"}};
  ConnectionManagerUtility::mutateResponseHeaders(response_headers, &request_headers, config_, via_,
                                                  false);
  EXPECT_TRUE(response_headers.has(Headers::get().Connection));
  EXPECT_TRUE(response_headers.has(Headers::get().TransferEncoding));
  EXPECT_TRUE(response_headers.has(Headers::get().Upgrade));
  EXPECT_TRUE(response_headers.has(Headers::get().KeepAlive));
  EXPECT_TRUE(response_headers.has(Headers::get().ProxyConnection));
}

// Verify that we don't set the via header on requests/responses when empty.
TEST_F(ConnectionManagerUtilityTest, ViaEmpty) {
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));

  TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ((MutateRequestRet{"10.0.0.1:0", true, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(request_headers, Protocol::Http2));
  EXPECT_FALSE(request_headers.has(Headers::get().Via));

  TestResponseHeaderMapImpl response_headers;
  ConnectionManagerUtility::mutateResponseHeaders(response_headers, &request_headers, config_,
                                                  via_);
  EXPECT_FALSE(response_headers.has(Headers::get().Via));
}

// Verify that we append a non-empty via header on requests/responses.
TEST_F(ConnectionManagerUtilityTest, ViaAppend) {
  via_ = "foo";
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));

  TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ((MutateRequestRet{"10.0.0.1:0", true, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(request_headers, Protocol::Http2));
  EXPECT_EQ("foo", request_headers.get_(Headers::get().Via));

  TestResponseHeaderMapImpl response_headers;
  // Pretend we're doing a 100-continue transform here.
  ConnectionManagerUtility::mutateResponseHeaders(response_headers, &request_headers, config_, "");
  // The actual response header processing.
  ConnectionManagerUtility::mutateResponseHeaders(response_headers, &request_headers, config_,
                                                  via_);
  EXPECT_EQ("foo", response_headers.get_(Headers::get().Via));
}

// Verify that we don't set user agent when it is already set.
TEST_F(ConnectionManagerUtilityTest, UserAgentDontSet) {
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  TestRequestHeaderMapImpl headers{{"user-agent", "foo"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.1:0", true, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ("foo", headers.get_(Headers::get().UserAgent));
  EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceCluster));
  EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceNode));
}

// Verify that we do set user agent when it is empty.
TEST_F(ConnectionManagerUtilityTest, UserAgentSetWhenIncomingEmpty) {
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  ON_CALL(local_info_, nodeName()).WillByDefault(ReturnRef(canary_node_));
  user_agent_ = "bar";
  TestRequestHeaderMapImpl headers{{"user-agent", ""},
                                   {"x-envoy-downstream-service-cluster", "foo"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.1:0", true, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ("bar", headers.get_(Headers::get().UserAgent));
  EXPECT_EQ("bar", headers.get_(Headers::get().EnvoyDownstreamServiceCluster));
  EXPECT_EQ("canary", headers.get_(Headers::get().EnvoyDownstreamServiceNode));
}

// Test not-cleaning/cleaning the force trace headers in different scenarios.
TEST_F(ConnectionManagerUtilityTest, InternalServiceForceTrace) {
  const std::string uuid = "f4dca0a9-12c7-4307-8002-969403baf480";
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(false));

  {
    // Internal request, make traceable.
    TestRequestHeaderMapImpl headers{
        {"x-forwarded-for", "10.0.0.1"}, {"x-request-id", uuid}, {"x-envoy-force-trace", "true"}};
    EXPECT_CALL(random_, uuid()).Times(0);
    EXPECT_CALL(runtime_.snapshot_,
                featureEnabled("tracing.global_enabled",
                               An<const envoy::type::v3::FractionalPercent&>(), _))
        .WillOnce(Return(true));

    EXPECT_EQ((MutateRequestRet{"10.0.0.1:0", true, Tracing::Reason::ServiceForced}),
              callMutateRequestHeaders(headers, Protocol::Http2));
    // Traceable (forced trace) variant of uuid
    EXPECT_EQ("f4dca0a9-12c7-a307-8002-969403baf480", headers.get_(Headers::get().RequestId));
  }

  {
    // Not internal request, force trace header should be cleaned.
    TestRequestHeaderMapImpl headers{
        {"x-forwarded-for", "34.0.0.1"}, {"x-request-id", uuid}, {"x-envoy-force-trace", "true"}};
    EXPECT_CALL(random_, uuid()).Times(0);
    EXPECT_CALL(runtime_.snapshot_,
                featureEnabled("tracing.random_sampling",
                               An<const envoy::type::v3::FractionalPercent&>(), _))
        .WillOnce(Return(false));

    EXPECT_EQ((MutateRequestRet{"34.0.0.1:0", false, Tracing::Reason::NotTraceable}),
              callMutateRequestHeaders(headers, Protocol::Http2));
    EXPECT_EQ(uuid, headers.get_(Headers::get().RequestId));
    EXPECT_FALSE(headers.has(Headers::get().EnvoyForceTrace));
  }
}

// Test generating request-id in various edge request scenarios.
TEST_F(ConnectionManagerUtilityTest, EdgeRequestRegenerateRequestIdAndWipeDownstream) {
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("34.0.0.1"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  ON_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled",
                                             An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillByDefault(Return(true));

  {
    TestRequestHeaderMapImpl headers{{"x-envoy-downstream-service-cluster", "foo"},
                                     {"x-request-id", "will_be_regenerated"}};
    EXPECT_CALL(random_, uuid());

    EXPECT_CALL(runtime_.snapshot_,
                featureEnabled("tracing.client_enabled", testing::Matcher<uint64_t>(_)))
        .Times(0);
    EXPECT_EQ((MutateRequestRet{"34.0.0.1:0", false, Tracing::Reason::NotTraceable}),
              callMutateRequestHeaders(headers, Protocol::Http2));
    EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceCluster));
    EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceNode));
    // No changes to uuid as x-client-trace-id is missing.
    EXPECT_EQ(random_.uuid_, headers.get_(Headers::get().RequestId));
  }

  {
    // Runtime does not allow to make request traceable even though x-client-trace-id set.
    TestRequestHeaderMapImpl headers{{"x-envoy-downstream-service-cluster", "foo"},
                                     {"x-request-id", "will_be_regenerated"},
                                     {"x-client-trace-id", "trace-id"}};
    EXPECT_CALL(random_, uuid());
    EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.client_enabled",
                                                   An<const envoy::type::v3::FractionalPercent&>()))
        .WillOnce(Return(false));

    EXPECT_EQ((MutateRequestRet{"34.0.0.1:0", false, Tracing::Reason::NotTraceable}),
              callMutateRequestHeaders(headers, Protocol::Http2));
    EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceCluster));
    EXPECT_EQ(random_.uuid_, headers.get_(Headers::get().RequestId));
  }

  {
    // Runtime is enabled for tracing and x-client-trace-id set.
    TestRequestHeaderMapImpl headers{{"x-envoy-downstream-service-cluster", "foo"},
                                     {"x-request-id", "will_be_regenerated"},
                                     {"x-client-trace-id", "trace-id"}};
    EXPECT_CALL(random_, uuid());
    EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.client_enabled",
                                                   An<const envoy::type::v3::FractionalPercent&>()))
        .WillOnce(Return(true));

    EXPECT_EQ((MutateRequestRet{"34.0.0.1:0", false, Tracing::Reason::ClientForced}),
              callMutateRequestHeaders(headers, Protocol::Http2));
    EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceCluster));
    // Traceable (client trace) variant of random_.uuid_
    EXPECT_EQ("a121e9e1-feae-b136-9e0e-6fac343d56c9", headers.get_(Headers::get().RequestId));
  }
}

// This tests that an external request, but not an edge request (because not using remote address)
// does not overwrite x-request-id. This happens in the internal ingress case.
TEST_F(ConnectionManagerUtilityTest, ExternalRequestPreserveRequestIdAndDownstream) {
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(false));
  TestRequestHeaderMapImpl headers{{"x-envoy-downstream-service-cluster", "foo"},
                                   {"x-request-id", "id"},
                                   {"x-forwarded-for", "34.0.0.1"}};

  EXPECT_CALL(local_info_, nodeName()).Times(0);

  EXPECT_EQ((MutateRequestRet{"34.0.0.1:0", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ("foo", headers.get_(Headers::get().EnvoyDownstreamServiceCluster));
  EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceNode));
  EXPECT_EQ("id", headers.get_(Headers::get().RequestId));
}

// Verify that we don't overwrite user agent, but do set x-envoy-downstream-service-cluster
// correctly.
TEST_F(ConnectionManagerUtilityTest, UserAgentSetIncomingUserAgent) {
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));

  user_agent_ = "bar";
  TestRequestHeaderMapImpl headers{{"user-agent", "foo"},
                                   {"x-envoy-downstream-service-cluster", "foo"}};
  EXPECT_CALL(local_info_, nodeName()).WillOnce(ReturnRef(empty_node_));

  EXPECT_EQ((MutateRequestRet{"10.0.0.1:0", true, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceNode));
  EXPECT_EQ("foo", headers.get_(Headers::get().UserAgent));
  EXPECT_EQ("bar", headers.get_(Headers::get().EnvoyDownstreamServiceCluster));
}

// Verify that we set both user agent and x-envoy-downstream-service-cluster.
TEST_F(ConnectionManagerUtilityTest, UserAgentSetNoIncomingUserAgent) {
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  user_agent_ = "bar";
  TestRequestHeaderMapImpl headers;

  EXPECT_EQ((MutateRequestRet{"10.0.0.1:0", true, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ("bar", headers.get_(Headers::get().UserAgent));
  EXPECT_EQ("bar", headers.get_(Headers::get().EnvoyDownstreamServiceCluster));
}

// Test different permutations of request-id generation.
TEST_F(ConnectionManagerUtilityTest, RequestIdGeneratedWhenItsNotPresent) {
  {
    TestRequestHeaderMapImpl headers{{":authority", "host"}, {":path", "/"}};
    EXPECT_CALL(random_, uuid()).WillOnce(Return("generated_uuid"));

    EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false, Tracing::Reason::NotTraceable}),
              callMutateRequestHeaders(headers, Protocol::Http2));
    EXPECT_EQ("generated_uuid", headers.get_("x-request-id"));
  }

  {
    Random::RandomGeneratorImpl rand;
    TestRequestHeaderMapImpl headers{{"x-client-trace-id", "trace-id"}};
    const std::string uuid = rand.uuid();
    EXPECT_CALL(random_, uuid()).WillOnce(Return(uuid));

    EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false, Tracing::Reason::NotTraceable}),
              callMutateRequestHeaders(headers, Protocol::Http2));
    // x-request-id should not be set to be traceable as it's not edge request
    EXPECT_EQ(uuid, headers.get_("x-request-id"));
  }
}

// Make sure we do not overwrite x-request-id if the request is internal.
TEST_F(ConnectionManagerUtilityTest, DoNotOverrideRequestIdIfPresentWhenInternalRequest) {
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  TestRequestHeaderMapImpl headers{{"x-request-id", "original_request_id"}};
  EXPECT_CALL(random_, uuid()).Times(0);

  EXPECT_EQ((MutateRequestRet{"10.0.0.1:0", true, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ("original_request_id", headers.get_("x-request-id"));
}

// Make sure that we do overwrite x-request-id for "edge" external requests.
TEST_F(ConnectionManagerUtilityTest, OverrideRequestIdForExternalRequests) {
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("134.2.2.11"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  TestRequestHeaderMapImpl headers{{"x-request-id", "original"}};
  EXPECT_CALL(random_, uuid()).WillOnce(Return("override"));

  EXPECT_EQ((MutateRequestRet{"134.2.2.11:0", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ("override", headers.get_("x-request-id"));
}

// A request that uses remote address and is from an external address should be treated as an
// external request with all internal only headers cleaned.
TEST_F(ConnectionManagerUtilityTest, ExternalAddressExternalRequestUseRemote) {
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("50.0.0.1"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  route_config_.internal_only_headers_.push_back(LowerCaseString("custom_header"));
  TestRequestHeaderMapImpl headers{{"x-envoy-decorator-operation", "foo"},
                                   {"x-envoy-downstream-service-cluster", "foo"},
                                   {"x-envoy-hedge-on-per-try-timeout", "foo"},
                                   {"x-envoy-retriable-status-codes", "123,456"},
                                   {"x-envoy-retry-on", "foo"},
                                   {"x-envoy-retry-grpc-on", "foo"},
                                   {"x-envoy-max-retries", "foo"},
                                   {"x-envoy-upstream-alt-stat-name", "foo"},
                                   {"x-envoy-upstream-rq-timeout-alt-response", "204"},
                                   {"x-envoy-upstream-rq-timeout-ms", "foo"},
                                   {"x-envoy-expected-rq-timeout-ms", "10"},
                                   {"x-envoy-ip-tags", "bar"},
                                   {"x-envoy-original-url", "my_url"},
                                   {"custom_header", "foo"}};

  EXPECT_EQ((MutateRequestRet{"50.0.0.1:0", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ("50.0.0.1", headers.get_("x-envoy-external-address"));
  EXPECT_FALSE(headers.has("x-envoy-decorator-operation"));
  EXPECT_FALSE(headers.has("x-envoy-downstream-service-cluster"));
  EXPECT_FALSE(headers.has("x-envoy-hedge-on-per-try-timeout"));
  EXPECT_FALSE(headers.has("x-envoy-retriable-status-codes"));
  EXPECT_FALSE(headers.has("x-envoy-retry-on"));
  EXPECT_FALSE(headers.has("x-envoy-retry-grpc-on"));
  EXPECT_FALSE(headers.has("x-envoy-max-retries"));
  EXPECT_FALSE(headers.has("x-envoy-upstream-alt-stat-name"));
  EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-alt-response"));
  EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
  EXPECT_FALSE(headers.has("x-envoy-expected-rq-timeout-ms"));
  EXPECT_FALSE(headers.has("x-envoy-ip-tags"));
  EXPECT_FALSE(headers.has("x-envoy-original-url"));
  EXPECT_FALSE(headers.has("custom_header"));
}

// A request that is from an external address, but does not use remote address, should pull the
// address from XFF.
TEST_F(ConnectionManagerUtilityTest, ExternalAddressExternalRequestDontUseRemote) {
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("60.0.0.2"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(false));
  TestRequestHeaderMapImpl headers{{"x-envoy-external-address", "60.0.0.1"},
                                   {"x-forwarded-for", "60.0.0.1"}};

  EXPECT_EQ((MutateRequestRet{"60.0.0.1:0", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ("60.0.0.1", headers.get_("x-envoy-external-address"));
  EXPECT_EQ("60.0.0.1", headers.get_("x-forwarded-for"));
}

// Verify that if XFF is invalid we fall back to remote address, even if it is a pipe.
TEST_F(ConnectionManagerUtilityTest, PipeAddressInvalidXFFtDontUseRemote) {
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::PipeInstance>("/blah"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(false));
  TestRequestHeaderMapImpl headers{{"x-forwarded-for", "blah"}};

  EXPECT_EQ((MutateRequestRet{"/blah", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_FALSE(headers.has("x-envoy-external-address"));
}

// Verify that we treat a request as external even if the direct remote is internal and XFF
// includes only internal addresses. Note that this is legacy behavior. See the comments
// in mutateRequestHeaders() for more info.
TEST_F(ConnectionManagerUtilityTest, AppendInternalAddressXffNotInternalRequest) {
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  TestRequestHeaderMapImpl headers{{"x-forwarded-for", "10.0.0.2"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.1:0", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ("10.0.0.2,10.0.0.1", headers.get_("x-forwarded-for"));
}

// A request that is from an internal address and uses remote address should be an internal request.
// It should also preserve x-envoy-external-address.
TEST_F(ConnectionManagerUtilityTest, ExternalAddressInternalRequestUseRemote) {
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  TestRequestHeaderMapImpl headers{{"x-envoy-external-address", "60.0.0.1"},
                                   {"x-envoy-expected-rq-timeout-ms", "10"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.1:0", true, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ("60.0.0.1", headers.get_("x-envoy-external-address"));
  EXPECT_EQ("10.0.0.1", headers.get_("x-forwarded-for"));
  EXPECT_EQ("10", headers.get_("x-envoy-expected-rq-timeout-ms"));
}

// Make sure we don't remove connection headers for WS requests.
TEST_F(ConnectionManagerUtilityTest, DoNotRemoveConnectionUpgradeForWebSocketRequests) {
  TestRequestHeaderMapImpl headers{{"connection", "upgrade"}, {"upgrade", "websocket"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http11));
  EXPECT_EQ("upgrade", headers.get_("connection"));
  EXPECT_EQ("websocket", headers.get_("upgrade"));
}

// Make sure we do remove connection headers for non-WS requests.
TEST_F(ConnectionManagerUtilityTest, RemoveConnectionUpgradeForNonWebSocketRequests) {
  TestRequestHeaderMapImpl headers{{"connection", "close"}, {"upgrade", "websocket"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http11));
  EXPECT_FALSE(headers.has("connection"));
  EXPECT_FALSE(headers.has("upgrade"));
}

// Test cleaning response headers.
TEST_F(ConnectionManagerUtilityTest, MutateResponseHeaders) {
  TestResponseHeaderMapImpl response_headers{
      {"connection", "foo"}, {"transfer-encoding", "foo"}, {"custom_header", "custom_value"}};
  TestRequestHeaderMapImpl request_headers{{"x-request-id", "request-id"}};

  ConnectionManagerUtility::mutateResponseHeaders(response_headers, &request_headers, config_, "");

  EXPECT_EQ(1UL, response_headers.size());
  EXPECT_EQ("custom_value", response_headers.get_("custom_header"));
  EXPECT_FALSE(response_headers.has("x-request-id"));
  EXPECT_FALSE(response_headers.has(Headers::get().Via));
}

// Make sure we don't remove connection headers on all Upgrade responses.
TEST_F(ConnectionManagerUtilityTest, DoNotRemoveConnectionUpgradeForWebSocketResponses) {
  TestRequestHeaderMapImpl request_headers{{"connection", "UpGrAdE"}, {"upgrade", "foo"}};
  TestResponseHeaderMapImpl response_headers{{":status", "101"},
                                             {"connection", "upgrade"},
                                             {"transfer-encoding", "foo"},
                                             {"upgrade", "bar"}};
  EXPECT_TRUE(Utility::isUpgrade(request_headers));
  EXPECT_TRUE(Utility::isUpgrade(response_headers));
  ConnectionManagerUtility::mutateResponseHeaders(response_headers, &request_headers, config_, "");

  EXPECT_EQ(3UL, response_headers.size()) << response_headers;
  EXPECT_EQ("upgrade", response_headers.get_("connection"));
  EXPECT_EQ("bar", response_headers.get_("upgrade"));
  EXPECT_EQ("101", response_headers.get_(":status"));
}

// Make sure we don't add a content-length header on Upgrade responses.
TEST_F(ConnectionManagerUtilityTest, DoNotAddConnectionLengthForWebSocket101Responses) {
  TestRequestHeaderMapImpl request_headers{{"connection", "UpGrAdE"}, {"upgrade", "foo"}};
  TestResponseHeaderMapImpl response_headers{
      {":status", "101"}, {"connection", "upgrade"}, {"upgrade", "bar"}};
  EXPECT_TRUE(Utility::isUpgrade(request_headers));
  EXPECT_TRUE(Utility::isUpgrade(response_headers));
  ConnectionManagerUtility::mutateResponseHeaders(response_headers, &request_headers, config_, "");

  EXPECT_EQ(3UL, response_headers.size()) << response_headers;
  EXPECT_EQ("upgrade", response_headers.get_("connection"));
  EXPECT_EQ("bar", response_headers.get_("upgrade"));
  EXPECT_EQ("101", response_headers.get_(":status"));
}

TEST_F(ConnectionManagerUtilityTest, ClearUpgradeHeadersForNonUpgradeRequests) {
  // Test clearing non-upgrade request and response headers
  {
    TestRequestHeaderMapImpl request_headers{{"x-request-id", "request-id"}};
    TestResponseHeaderMapImpl response_headers{
        {"connection", "foo"}, {"transfer-encoding", "bar"}, {"custom_header", "custom_value"}};
    EXPECT_FALSE(Utility::isUpgrade(request_headers));
    EXPECT_FALSE(Utility::isUpgrade(response_headers));
    ConnectionManagerUtility::mutateResponseHeaders(response_headers, &request_headers, config_,
                                                    "");

    EXPECT_EQ(1UL, response_headers.size()) << response_headers;
    EXPECT_EQ("custom_value", response_headers.get_("custom_header"));
  }

  // Test with the request headers not valid upgrade headers
  {
    TestRequestHeaderMapImpl request_headers{{"upgrade", "foo"}};
    TestResponseHeaderMapImpl response_headers{{"connection", "upgrade"},
                                               {"transfer-encoding", "eep"},
                                               {"upgrade", "foo"},
                                               {"custom_header", "custom_value"}};
    EXPECT_FALSE(Utility::isUpgrade(request_headers));
    EXPECT_TRUE(Utility::isUpgrade(response_headers));
    ConnectionManagerUtility::mutateResponseHeaders(response_headers, &request_headers, config_,
                                                    "");

    EXPECT_EQ(1UL, response_headers.size()) << response_headers;
    EXPECT_EQ("custom_value", response_headers.get_("custom_header"));
  }

  // Test with the response headers not valid upgrade headers
  {
    TestRequestHeaderMapImpl request_headers{{"connection", "UpGrAdE"}, {"upgrade", "foo"}};
    TestResponseHeaderMapImpl response_headers{{"transfer-encoding", "foo"}, {"upgrade", "bar"}};
    EXPECT_TRUE(Utility::isUpgrade(request_headers));
    EXPECT_FALSE(Utility::isUpgrade(response_headers));
    ConnectionManagerUtility::mutateResponseHeaders(response_headers, &request_headers, config_,
                                                    "");

    EXPECT_EQ(0UL, response_headers.size()) << response_headers;
  }
}

// Test that we correctly return x-request-id if we were requested to force a trace.
TEST_F(ConnectionManagerUtilityTest, MutateResponseHeadersReturnXRequestId) {
  TestResponseHeaderMapImpl response_headers;
  TestRequestHeaderMapImpl request_headers{{"x-request-id", "request-id"},
                                           {"x-envoy-force-trace", "true"}};

  EXPECT_CALL(*request_id_extension_,
              setInResponse(testing::Ref(response_headers), testing::Ref(request_headers)));
  ConnectionManagerUtility::mutateResponseHeaders(response_headers, &request_headers, config_, "");
  EXPECT_EQ("request-id", response_headers.get_("x-request-id"));
}

// Test that we do not return x-request-id if we were not requested to force a trace.
TEST_F(ConnectionManagerUtilityTest, SkipMutateResponseHeadersReturnXRequestId) {
  TestResponseHeaderMapImpl response_headers;
  TestRequestHeaderMapImpl request_headers{{"x-request-id", "request-id"}};

  EXPECT_CALL(*request_id_extension_,
              setInResponse(testing::Ref(response_headers), testing::Ref(request_headers)))
      .Times(0);
  ConnectionManagerUtility::mutateResponseHeaders(response_headers, &request_headers, config_, "");
  EXPECT_EQ("", response_headers.get_("x-request-id"));
}

// Test that we do return x-request-id if we were asked to always return it even if trace is not
// forced.
TEST_F(ConnectionManagerUtilityTest, AlwaysMutateResponseHeadersReturnXRequestId) {
  TestResponseHeaderMapImpl response_headers;
  TestRequestHeaderMapImpl request_headers{{"x-request-id", "request-id"}};

  EXPECT_CALL(*request_id_extension_,
              setInResponse(testing::Ref(response_headers), testing::Ref(request_headers)));
  ON_CALL(config_, alwaysSetRequestIdInResponse()).WillByDefault(Return(true));
  ConnectionManagerUtility::mutateResponseHeaders(response_headers, &request_headers, config_, "");
  EXPECT_EQ("request-id", response_headers.get_("x-request-id"));
}

// Test full sanitization of x-forwarded-client-cert.
TEST_F(ConnectionManagerUtilityTest, MtlsSanitizeClientCert) {
  auto ssl = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*ssl, peerCertificatePresented()).WillByDefault(Return(true));
  ON_CALL(connection_, ssl()).WillByDefault(Return(ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::Sanitize));
  std::vector<Http::ClientCertDetailsType> details;
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestRequestHeaderMapImpl headers{{"x-forwarded-client-cert", "By=test;URI=abc;DNS=example.com"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_FALSE(headers.has("x-forwarded-client-cert"));
}

// Test that we sanitize and set x-forwarded-client-cert.
TEST_F(ConnectionManagerUtilityTest, MtlsForwardOnlyClientCert) {
  auto ssl = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*ssl, peerCertificatePresented()).WillByDefault(Return(true));
  ON_CALL(connection_, ssl()).WillByDefault(Return(ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::ForwardOnly));
  std::vector<Http::ClientCertDetailsType> details;
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestRequestHeaderMapImpl headers{
      {"x-forwarded-client-cert", "By=test://foo.com/fe;URI=test://bar.com/be;DNS=example.com"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/fe;URI=test://bar.com/be;DNS=example.com",
            headers.get_("x-forwarded-client-cert"));
}

// The server (local) identity is foo.com/be. The client does not set XFCC.
TEST_F(ConnectionManagerUtilityTest, MtlsSetForwardClientCert) {
  auto ssl = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*ssl, peerCertificatePresented()).WillByDefault(Return(true));
  const std::vector<std::string> local_uri_sans{"test://foo.com/be"};
  EXPECT_CALL(*ssl, uriSanLocalCertificate()).WillOnce(Return(local_uri_sans));
  std::string expected_sha("abcdefg");
  EXPECT_CALL(*ssl, sha256PeerCertificateDigest()).WillOnce(ReturnRef(expected_sha));
  const std::vector<std::string> peer_uri_sans{"test://foo.com/fe"};
  EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(peer_uri_sans));
  std::string expected_pem("%3D%3Dabc%0Ade%3D");
  EXPECT_CALL(*ssl, urlEncodedPemEncodedPeerCertificate()).WillOnce(ReturnRef(expected_pem));
  std::string expected_chain_pem(expected_pem + "%3D%3Dlmn%0Aop%3D");
  EXPECT_CALL(*ssl, urlEncodedPemEncodedPeerCertificateChain())
      .WillOnce(ReturnRef(expected_chain_pem));
  std::vector<std::string> expected_dns = {"www.example.com"};
  EXPECT_CALL(*ssl, dnsSansPeerCertificate()).WillOnce(Return(expected_dns));
  ON_CALL(connection_, ssl()).WillByDefault(Return(ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::AppendForward));
  std::vector<Http::ClientCertDetailsType> details = std::vector<Http::ClientCertDetailsType>();
  details.push_back(Http::ClientCertDetailsType::URI);
  details.push_back(Http::ClientCertDetailsType::Cert);
  details.push_back(Http::ClientCertDetailsType::Chain);
  details.push_back(Http::ClientCertDetailsType::DNS);
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestRequestHeaderMapImpl headers;

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/be;"
            "Hash=abcdefg;"
            "URI=test://foo.com/fe;"
            "Cert=\"%3D%3Dabc%0Ade%3D\";"
            "Chain=\"%3D%3Dabc%0Ade%3D%3D%3Dlmn%0Aop%3D\";"
            "DNS=www.example.com",
            headers.get_("x-forwarded-client-cert"));
}

// This test assumes the following scenario:
// The client identity is foo.com/fe, and the server (local) identity is foo.com/be. The client
// also sends the XFCC header with the authentication result of the previous hop, (bar.com/be
// calling foo.com/fe).
TEST_F(ConnectionManagerUtilityTest, MtlsAppendForwardClientCert) {
  auto ssl = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*ssl, peerCertificatePresented()).WillByDefault(Return(true));
  const std::vector<std::string> local_uri_sans{"test://foo.com/be"};
  EXPECT_CALL(*ssl, uriSanLocalCertificate()).WillOnce(Return(local_uri_sans));
  std::string expected_sha("abcdefg");
  EXPECT_CALL(*ssl, sha256PeerCertificateDigest()).WillOnce(ReturnRef(expected_sha));
  const std::vector<std::string> peer_uri_sans{"test://foo.com/fe"};
  EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(peer_uri_sans));
  std::string expected_pem("%3D%3Dabc%0Ade%3D");
  EXPECT_CALL(*ssl, urlEncodedPemEncodedPeerCertificate()).WillOnce(ReturnRef(expected_pem));
  std::string expected_chain_pem(expected_pem + "%3D%3Dlmn%0Aop%3D");
  EXPECT_CALL(*ssl, urlEncodedPemEncodedPeerCertificateChain())
      .WillOnce(ReturnRef(expected_chain_pem));
  std::vector<std::string> expected_dns = {"www.example.com"};
  EXPECT_CALL(*ssl, dnsSansPeerCertificate()).WillOnce(Return(expected_dns));
  ON_CALL(connection_, ssl()).WillByDefault(Return(ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::AppendForward));
  std::vector<Http::ClientCertDetailsType> details = std::vector<Http::ClientCertDetailsType>();
  details.push_back(Http::ClientCertDetailsType::URI);
  details.push_back(Http::ClientCertDetailsType::Cert);
  details.push_back(Http::ClientCertDetailsType::Chain);
  details.push_back(Http::ClientCertDetailsType::DNS);
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestRequestHeaderMapImpl headers{{"x-forwarded-client-cert", "By=test://foo.com/fe;"
                                                               "URI=test://bar.com/be;"
                                                               "DNS=test.com;DNS=test.com"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ(
      "By=test://foo.com/fe;URI=test://bar.com/be;DNS=test.com;DNS=test.com,"
      "By=test://foo.com/be;Hash=abcdefg;URI=test://foo.com/fe;"
      "Cert=\"%3D%3Dabc%0Ade%3D\";Chain=\"%3D%3Dabc%0Ade%3D%3D%3Dlmn%0Aop%3D\";DNS=www.example.com",
      headers.get_("x-forwarded-client-cert"));
}

// This test assumes the following scenario:
// The client identity is foo.com/fe, and the server (local) identity is foo.com/be. The client
// also sends the XFCC header with the authentication result of the previous hop, (bar.com/be
// calling foo.com/fe).
TEST_F(ConnectionManagerUtilityTest, MtlsAppendForwardClientCertLocalSanEmpty) {
  auto ssl = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*ssl, peerCertificatePresented()).WillByDefault(Return(true));
  EXPECT_CALL(*ssl, uriSanLocalCertificate()).WillOnce(Return(std::vector<std::string>()));
  std::string expected_sha("abcdefg");
  EXPECT_CALL(*ssl, sha256PeerCertificateDigest()).WillOnce(ReturnRef(expected_sha));
  const std::vector<std::string> peer_uri_sans{"test://foo.com/fe"};
  EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(peer_uri_sans));
  ON_CALL(connection_, ssl()).WillByDefault(Return(ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::AppendForward));
  std::vector<Http::ClientCertDetailsType> details = std::vector<Http::ClientCertDetailsType>();
  details.push_back(Http::ClientCertDetailsType::URI);
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestRequestHeaderMapImpl headers{
      {"x-forwarded-client-cert", "By=test://foo.com/fe;Hash=xyz;URI=test://bar.com/be"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/fe;Hash=xyz;URI=test://bar.com/be,"
            "Hash=abcdefg;URI=test://foo.com/fe",
            headers.get_("x-forwarded-client-cert"));
}

// This test assumes the following scenario:
// The client identity is foo.com/fe, and the server (local) identity is foo.com/be. The client
// also sends the XFCC header with the authentication result of the previous hop, (bar.com/be
// calling foo.com/fe).
TEST_F(ConnectionManagerUtilityTest, MtlsSanitizeSetClientCert) {
  auto ssl = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*ssl, peerCertificatePresented()).WillByDefault(Return(true));
  const std::vector<std::string> local_uri_sans{"test://foo.com/be"};
  EXPECT_CALL(*ssl, uriSanLocalCertificate()).WillOnce(Return(local_uri_sans));
  std::string expected_sha("abcdefg");
  EXPECT_CALL(*ssl, sha256PeerCertificateDigest()).WillOnce(ReturnRef(expected_sha));
  std::string peer_subject("/C=US/ST=CA/L=San Francisco/OU=Lyft/CN=test.lyft.com");
  EXPECT_CALL(*ssl, subjectPeerCertificate()).WillOnce(ReturnRef(peer_subject));
  const std::vector<std::string> peer_uri_sans{"test://foo.com/fe"};
  EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(peer_uri_sans));
  std::string expected_pem("abcde=");
  EXPECT_CALL(*ssl, urlEncodedPemEncodedPeerCertificate()).WillOnce(ReturnRef(expected_pem));
  std::string expected_chain_pem(expected_pem + "lmnop=");
  EXPECT_CALL(*ssl, urlEncodedPemEncodedPeerCertificateChain())
      .WillOnce(ReturnRef(expected_chain_pem));
  ON_CALL(connection_, ssl()).WillByDefault(Return(ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::SanitizeSet));
  std::vector<Http::ClientCertDetailsType> details = std::vector<Http::ClientCertDetailsType>();
  details.push_back(Http::ClientCertDetailsType::Subject);
  details.push_back(Http::ClientCertDetailsType::URI);
  details.push_back(Http::ClientCertDetailsType::Cert);
  details.push_back(Http::ClientCertDetailsType::Chain);
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestRequestHeaderMapImpl headers{
      {"x-forwarded-client-cert", "By=test://foo.com/fe;URI=test://bar.com/be"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/be;Hash=abcdefg;Subject=\"/C=US/ST=CA/L=San "
            "Francisco/OU=Lyft/CN=test.lyft.com\";URI=test://foo.com/"
            "fe;Cert=\"abcde=\";Chain=\"abcde=lmnop=\"",
            headers.get_("x-forwarded-client-cert"));
}

// This test assumes the following scenario:
// The client identity is foo.com/fe, and the server (local) identity is foo.com/be. The client
// also sends the XFCC header with the authentication result of the previous hop, (bar.com/be
// calling foo.com/fe).
TEST_F(ConnectionManagerUtilityTest, MtlsSanitizeSetClientCertPeerSanEmpty) {
  auto ssl = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*ssl, peerCertificatePresented()).WillByDefault(Return(true));
  const std::vector<std::string> local_uri_sans{"test://foo.com/be"};
  EXPECT_CALL(*ssl, uriSanLocalCertificate()).WillOnce(Return(local_uri_sans));
  std::string expected_sha("abcdefg");
  EXPECT_CALL(*ssl, sha256PeerCertificateDigest()).WillOnce(ReturnRef(expected_sha));
  std::string peer_subject = "/C=US/ST=CA/L=San Francisco/OU=Lyft/CN=test.lyft.com";
  EXPECT_CALL(*ssl, subjectPeerCertificate()).WillOnce(ReturnRef(peer_subject));
  EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(std::vector<std::string>()));
  ON_CALL(connection_, ssl()).WillByDefault(Return(ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::SanitizeSet));
  std::vector<Http::ClientCertDetailsType> details = std::vector<Http::ClientCertDetailsType>();
  details.push_back(Http::ClientCertDetailsType::Subject);
  details.push_back(Http::ClientCertDetailsType::URI);
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestRequestHeaderMapImpl headers{
      {"x-forwarded-client-cert", "By=test://foo.com/fe;URI=test://bar.com/be"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/be;Hash=abcdefg;Subject=\"/C=US/ST=CA/L=San "
            "Francisco/OU=Lyft/CN=test.lyft.com\";URI=",
            headers.get_("x-forwarded-client-cert"));
}

// forward_only, append_forward and sanitize_set are only effective in mTLS connection.
TEST_F(ConnectionManagerUtilityTest, TlsSanitizeClientCertWhenForward) {
  auto ssl = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*ssl, peerCertificatePresented()).WillByDefault(Return(false));
  ON_CALL(connection_, ssl()).WillByDefault(Return(ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::ForwardOnly));
  std::vector<Http::ClientCertDetailsType> details;
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestRequestHeaderMapImpl headers{{"x-forwarded-client-cert", "By=test;URI=abc"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_FALSE(headers.has("x-forwarded-client-cert"));
}

// always_forward_only works regardless whether the connection is TLS/mTLS.
TEST_F(ConnectionManagerUtilityTest, TlsAlwaysForwardOnlyClientCert) {
  auto ssl = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*ssl, peerCertificatePresented()).WillByDefault(Return(false));
  ON_CALL(connection_, ssl()).WillByDefault(Return(ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::AlwaysForwardOnly));
  std::vector<Http::ClientCertDetailsType> details;
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestRequestHeaderMapImpl headers{
      {"x-forwarded-client-cert", "By=test://foo.com/fe;URI=test://bar.com/be"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/fe;URI=test://bar.com/be", headers.get_("x-forwarded-client-cert"));
}

// forward_only, append_forward and sanitize_set are only effective in mTLS connection.
TEST_F(ConnectionManagerUtilityTest, NonTlsSanitizeClientCertWhenForward) {
  ON_CALL(connection_, ssl()).WillByDefault(Return(nullptr));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::ForwardOnly));
  std::vector<Http::ClientCertDetailsType> details;
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestRequestHeaderMapImpl headers{{"x-forwarded-client-cert", "By=test;URI=abc"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_FALSE(headers.has("x-forwarded-client-cert"));
}

// always_forward_only works regardless whether the connection is TLS/mTLS.
TEST_F(ConnectionManagerUtilityTest, NonTlsAlwaysForwardClientCert) {
  ON_CALL(connection_, ssl()).WillByDefault(Return(nullptr));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::AlwaysForwardOnly));
  std::vector<Http::ClientCertDetailsType> details;
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestRequestHeaderMapImpl headers{
      {"x-forwarded-client-cert", "By=test://foo.com/fe;URI=test://bar.com/be"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/fe;URI=test://bar.com/be", headers.get_("x-forwarded-client-cert"));
}

// Sampling, global on.
TEST_F(ConnectionManagerUtilityTest, RandomSamplingWhenGlobalSet) {
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.random_sampling", An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.global_enabled", An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillOnce(Return(true));

  Http::TestRequestHeaderMapImpl request_headers{
      {"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"}};
  EXPECT_CALL(*request_id_extension_,
              setTraceReason(testing::Ref(request_headers), Tracing::Reason::Sampling));
  callMutateRequestHeaders(request_headers, Protocol::Http2);

  EXPECT_EQ(Tracing::Reason::Sampling, request_id_extension_->getTraceReason(request_headers));
}

TEST_F(ConnectionManagerUtilityTest, SamplingWithoutRouteOverride) {
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.random_sampling", An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.global_enabled", An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillOnce(Return(true));

  Http::TestRequestHeaderMapImpl request_headers{
      {"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"}};
  EXPECT_CALL(*request_id_extension_,
              setTraceReason(testing::Ref(request_headers), Tracing::Reason::Sampling));
  callMutateRequestHeaders(request_headers, Protocol::Http2);

  EXPECT_EQ(Tracing::Reason::Sampling, request_id_extension_->getTraceReason(request_headers));
}

TEST_F(ConnectionManagerUtilityTest, CheckSamplingDecisionWithBypassSamplingWithRequestId) {
  EXPECT_CALL(*request_id_extension_, useRequestIdForTraceSampling()).WillOnce(Return(false));
  Http::TestRequestHeaderMapImpl request_headers{
      {"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"}};
  const auto ret = callMutateRequestHeaders(request_headers, Protocol::Http2);
  EXPECT_EQ(Tracing::Reason::Sampling, ret.trace_reason_);
}

TEST_F(ConnectionManagerUtilityTest, SamplingWithRouteOverride) {
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.random_sampling", An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillOnce(Return(false));

  NiceMock<Router::MockRouteTracing> tracingConfig;
  EXPECT_CALL(route_, tracingConfig()).WillRepeatedly(Return(&tracingConfig));
  const envoy::type::v3::FractionalPercent percent;
  EXPECT_CALL(tracingConfig, getClientSampling()).WillRepeatedly(ReturnRef(percent));
  EXPECT_CALL(tracingConfig, getRandomSampling()).WillRepeatedly(ReturnRef(percent));
  EXPECT_CALL(tracingConfig, getOverallSampling()).WillRepeatedly(ReturnRef(percent));

  Http::TestRequestHeaderMapImpl request_headers{
      {"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"}};
  callMutateRequestHeaders(request_headers, Protocol::Http2);

  EXPECT_EQ(Tracing::Reason::NotTraceable, request_id_extension_->getTraceReason(request_headers));
}

// Sampling must not be done on client traced.
TEST_F(ConnectionManagerUtilityTest, SamplingMustNotBeDoneOnClientTraced) {
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.random_sampling", An<const envoy::type::v3::FractionalPercent&>(), _))
      .Times(0);
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.global_enabled", An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillOnce(Return(true));

  // The x_request_id has TRACE_FORCED(a) set in the TRACE_BYTE_POSITION(14) character.
  Http::TestRequestHeaderMapImpl request_headers{
      {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}};
  EXPECT_CALL(*request_id_extension_, setTraceReason(_, _)).Times(0);
  callMutateRequestHeaders(request_headers, Protocol::Http2);

  EXPECT_EQ(Tracing::Reason::ServiceForced, request_id_extension_->getTraceReason(request_headers));
}

// Sampling, global off.
TEST_F(ConnectionManagerUtilityTest, NoTraceWhenSamplingSetButGlobalNotSet) {
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.random_sampling", An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.global_enabled", An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillOnce(Return(false));

  Http::TestRequestHeaderMapImpl request_headers{
      {"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"}};
  EXPECT_CALL(*request_id_extension_,
              setTraceReason(testing::Ref(request_headers), Tracing::Reason::Sampling));
  EXPECT_CALL(*request_id_extension_,
              setTraceReason(testing::Ref(request_headers), Tracing::Reason::NotTraceable));
  callMutateRequestHeaders(request_headers, Protocol::Http2);

  EXPECT_EQ(Tracing::Reason::NotTraceable, request_id_extension_->getTraceReason(request_headers));
}

// Client, client enabled, global on.
TEST_F(ConnectionManagerUtilityTest, ClientSamplingWhenGlobalSet) {
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.client_enabled",
                                                 An<const envoy::type::v3::FractionalPercent&>()))
      .WillOnce(Return(true));
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.global_enabled", An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillOnce(Return(true));

  Http::TestRequestHeaderMapImpl request_headers{
      {"x-client-trace-id", "f4dca0a9-12c7-4307-8002-969403baf480"},
      {"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"}};
  EXPECT_CALL(*request_id_extension_,
              setTraceReason(testing::Ref(request_headers), Tracing::Reason::ClientForced));
  callMutateRequestHeaders(request_headers, Protocol::Http2);

  EXPECT_EQ(Tracing::Reason::ClientForced, request_id_extension_->getTraceReason(request_headers));
}

// Client, client disabled, global on.
TEST_F(ConnectionManagerUtilityTest, NoTraceWhenClientSamplingNotSetAndGlobalSet) {
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.client_enabled",
                                                 An<const envoy::type::v3::FractionalPercent&>()))
      .WillOnce(Return(false));
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.random_sampling", An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillOnce(Return(false));

  Http::TestRequestHeaderMapImpl request_headers{
      {"x-client-trace-id", "f4dca0a9-12c7-4307-8002-969403baf480"},
      {"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"}};
  EXPECT_CALL(*request_id_extension_, setTraceReason(_, _)).Times(0);
  callMutateRequestHeaders(request_headers, Protocol::Http2);

  EXPECT_EQ(Tracing::Reason::NotTraceable, request_id_extension_->getTraceReason(request_headers));
}

// Forced, global on.
TEST_F(ConnectionManagerUtilityTest, ForcedTracedWhenGlobalSet) {
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(false));
  // Internal request, make traceable.
  TestRequestHeaderMapImpl headers{{"x-forwarded-for", "10.0.0.1"},
                                   {"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"},
                                   {"x-envoy-force-trace", "true"}};
  EXPECT_CALL(random_, uuid()).Times(0);
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.global_enabled", An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*request_id_extension_,
              setTraceReason(testing::Ref(headers), Tracing::Reason::ServiceForced));

  EXPECT_EQ((MutateRequestRet{"10.0.0.1:0", true, Tracing::Reason::ServiceForced}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ(Tracing::Reason::ServiceForced, request_id_extension_->getTraceReason(headers));
}

// Forced, global off.
TEST_F(ConnectionManagerUtilityTest, NoTraceWhenForcedTracedButGlobalNotSet) {
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(false));
  // Internal request, make traceable.
  TestRequestHeaderMapImpl headers{{"x-forwarded-for", "10.0.0.1"},
                                   {"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"},
                                   {"x-envoy-force-trace", "true"}};
  EXPECT_CALL(random_, uuid()).Times(0);
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.global_enabled", An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillOnce(Return(false));
  EXPECT_CALL(*request_id_extension_,
              setTraceReason(testing::Ref(headers), Tracing::Reason::ServiceForced));
  EXPECT_CALL(*request_id_extension_,
              setTraceReason(testing::Ref(headers), Tracing::Reason::NotTraceable));

  EXPECT_EQ((MutateRequestRet{"10.0.0.1:0", true, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ(Tracing::Reason::NotTraceable, request_id_extension_->getTraceReason(headers));
}

// Forced, global on, broken uuid.
TEST_F(ConnectionManagerUtilityTest, NoTraceOnBrokenUuid) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-force-trace", "true"},
                                                 {"x-request-id", "bb"}};
  EXPECT_CALL(*request_id_extension_, setTraceReason(_, _)).Times(0);
  callMutateRequestHeaders(request_headers, Protocol::Http2);

  EXPECT_EQ(Tracing::Reason::NotTraceable, request_id_extension_->getTraceReason(request_headers));
}

TEST_F(ConnectionManagerUtilityTest, RemovesProxyResponseHeaders) {
  Http::TestRequestHeaderMapImpl request_headers{{}};
  Http::TestResponseHeaderMapImpl response_headers{{"keep-alive", "timeout=60"},
                                                   {"proxy-connection", "proxy-header"}};
  EXPECT_CALL(*request_id_extension_, setTraceReason(_, _)).Times(0);
  ConnectionManagerUtility::mutateResponseHeaders(response_headers, &request_headers, config_, "");

  EXPECT_EQ(Tracing::Reason::NotTraceable, request_id_extension_->getTraceReason(request_headers));

  EXPECT_FALSE(response_headers.has("keep-alive"));
  EXPECT_FALSE(response_headers.has("proxy-connection"));
}

// maybeNormalizePath() returns true with an empty path.
TEST_F(ConnectionManagerUtilityTest, SanitizeEmptyPath) {
  ON_CALL(config_, shouldNormalizePath()).WillByDefault(Return(false));
  TestRequestHeaderMapImpl original_headers;

  TestRequestHeaderMapImpl header_map(original_headers);
  EXPECT_EQ(ConnectionManagerUtility::NormalizePathAction::Continue,
            ConnectionManagerUtility::maybeNormalizePath(header_map, config_));
  EXPECT_EQ(original_headers, header_map);
}

// maybeNormalizePath() does nothing by default.
TEST_F(ConnectionManagerUtilityTest, SanitizePathDefaultOff) {
  ON_CALL(config_, shouldNormalizePath()).WillByDefault(Return(false));
  TestRequestHeaderMapImpl original_headers;
  original_headers.setPath("/xyz/../a");

  TestRequestHeaderMapImpl header_map(original_headers);
  ConnectionManagerUtility::maybeNormalizePath(header_map, config_);
  EXPECT_EQ(original_headers, header_map);
}

// maybeNormalizePath() leaves already normal paths alone.
TEST_F(ConnectionManagerUtilityTest, SanitizePathNormalPath) {
  ON_CALL(config_, shouldNormalizePath()).WillByDefault(Return(true));
  TestRequestHeaderMapImpl original_headers;
  original_headers.setPath("/xyz");

  TestRequestHeaderMapImpl header_map(original_headers);
  ConnectionManagerUtility::maybeNormalizePath(header_map, config_);
  EXPECT_EQ(original_headers, header_map);
}

// maybeNormalizePath() normalizes relative paths.
TEST_F(ConnectionManagerUtilityTest, SanitizePathRelativePAth) {
  ON_CALL(config_, shouldNormalizePath()).WillByDefault(Return(true));
  TestRequestHeaderMapImpl original_headers;
  original_headers.setPath("/xyz/../abc");

  TestRequestHeaderMapImpl header_map(original_headers);
  ConnectionManagerUtility::maybeNormalizePath(header_map, config_);
  EXPECT_EQ(header_map.getPathValue(), "/abc");
}

// maybeNormalizePath() does not touch adjacent slashes by default.
TEST_F(ConnectionManagerUtilityTest, MergeSlashesDefaultOff) {
  ON_CALL(config_, shouldNormalizePath()).WillByDefault(Return(true));
  ON_CALL(config_, shouldMergeSlashes()).WillByDefault(Return(false));
  TestRequestHeaderMapImpl original_headers;
  original_headers.setPath("/xyz///abc");

  TestRequestHeaderMapImpl header_map(original_headers);
  ConnectionManagerUtility::maybeNormalizePath(header_map, config_);
  EXPECT_EQ(header_map.getPathValue(), "/xyz///abc");
}

// maybeNormalizePath() merges adjacent slashes.
TEST_F(ConnectionManagerUtilityTest, MergeSlashes) {
  ON_CALL(config_, shouldNormalizePath()).WillByDefault(Return(true));
  ON_CALL(config_, shouldMergeSlashes()).WillByDefault(Return(true));
  TestRequestHeaderMapImpl original_headers;
  original_headers.setPath("/xyz///abc");

  TestRequestHeaderMapImpl header_map(original_headers);
  ConnectionManagerUtility::maybeNormalizePath(header_map, config_);
  EXPECT_EQ(header_map.getPathValue(), "/xyz/abc");
}

// maybeNormalizePath() merges adjacent slashes if normalization if off.
TEST_F(ConnectionManagerUtilityTest, MergeSlashesWithoutNormalization) {
  ON_CALL(config_, shouldNormalizePath()).WillByDefault(Return(false));
  ON_CALL(config_, shouldMergeSlashes()).WillByDefault(Return(true));
  TestRequestHeaderMapImpl original_headers;
  original_headers.setPath("/xyz/..//abc");

  TestRequestHeaderMapImpl header_map(original_headers);
  ConnectionManagerUtility::maybeNormalizePath(header_map, config_);
  EXPECT_EQ(header_map.getPathValue(), "/xyz/../abc");
}

// maybeNormalizeHost() removes port part from host header.
TEST_F(ConnectionManagerUtilityTest, RemovePort) {
  ON_CALL(config_, stripPortType()).WillByDefault(Return(Http::StripPortType::MatchingHost));
  TestRequestHeaderMapImpl original_headers;
  original_headers.setHost("host:443");

  TestRequestHeaderMapImpl header_map(original_headers);
  ConnectionManagerUtility::maybeNormalizeHost(header_map, config_, 443);
  EXPECT_EQ(header_map.getHostValue(), "host");

  ON_CALL(config_, stripPortType()).WillByDefault(Return(Http::StripPortType::Any));
  TestRequestHeaderMapImpl original_headers_any;
  original_headers_any.setHost("host:9999");

  TestRequestHeaderMapImpl header_map_any(original_headers_any);
  ConnectionManagerUtility::maybeNormalizeHost(header_map_any, config_, 7777);
  EXPECT_EQ(header_map_any.getHostValue(), "host");

  ON_CALL(config_, stripPortType()).WillByDefault(Return(Http::StripPortType::None));
  TestRequestHeaderMapImpl original_headers_none;
  original_headers_none.setHost("host:9999");

  TestRequestHeaderMapImpl header_map_none(original_headers_none);
  ConnectionManagerUtility::maybeNormalizeHost(header_map_none, config_, 0);
  EXPECT_EQ(header_map_none.getHostValue(), "host:9999");
}

// maybeNormalizeHost() removes trailing dot of host from host header.
TEST_F(ConnectionManagerUtilityTest, RemoveTrailingDot) {
  ON_CALL(config_, shouldStripTrailingHostDot()).WillByDefault(Return(true));
  TestRequestHeaderMapImpl original_headers;
  original_headers.setHost("host.");

  TestRequestHeaderMapImpl header_map(original_headers);
  ConnectionManagerUtility::maybeNormalizeHost(header_map, config_, 0);
  EXPECT_EQ(header_map.getHostValue(), "host");

  ON_CALL(config_, stripPortType()).WillByDefault(Return(Http::StripPortType::None));
  ON_CALL(config_, shouldStripTrailingHostDot()).WillByDefault(Return(true));
  TestRequestHeaderMapImpl original_headers_with_port;
  original_headers_with_port.setHost("host.:443");

  TestRequestHeaderMapImpl header_map_with_port(original_headers_with_port);
  ConnectionManagerUtility::maybeNormalizeHost(header_map_with_port, config_, 443);
  EXPECT_EQ(header_map_with_port.getHostValue(), "host:443");

  ON_CALL(config_, stripPortType()).WillByDefault(Return(Http::StripPortType::MatchingHost));
  ON_CALL(config_, shouldStripTrailingHostDot()).WillByDefault(Return(true));
  TestRequestHeaderMapImpl original_headers_strip_port;
  original_headers_strip_port.setHost("host.:443");

  TestRequestHeaderMapImpl header_map_strip_port(original_headers_strip_port);
  ConnectionManagerUtility::maybeNormalizeHost(header_map_strip_port, config_, 443);
  EXPECT_EQ(header_map_strip_port.getHostValue(), "host");

  ON_CALL(config_, shouldStripTrailingHostDot()).WillByDefault(Return(true));
  TestRequestHeaderMapImpl original_headers_no_dot;
  original_headers_no_dot.setHost("host");

  TestRequestHeaderMapImpl header_map_no_dot(original_headers_no_dot);
  ConnectionManagerUtility::maybeNormalizeHost(header_map_no_dot, config_, 0);
  EXPECT_EQ(header_map_no_dot.getHostValue(), "host");

  ON_CALL(config_, shouldStripTrailingHostDot()).WillByDefault(Return(false));
  TestRequestHeaderMapImpl original_headers_none;
  original_headers_none.setHost("host.");

  TestRequestHeaderMapImpl header_map_none(original_headers_none);
  ConnectionManagerUtility::maybeNormalizeHost(header_map_none, config_, 0);
  EXPECT_EQ(header_map_none.getHostValue(), "host.");
}

// maybeNormalizePath() does not touch escaped slashes when configured to KEEP_UNCHANGED.
TEST_F(ConnectionManagerUtilityTest, KeepEscapedSlashesWhenConfigured) {
  ON_CALL(config_, pathWithEscapedSlashesAction())
      .WillByDefault(Return(envoy::extensions::filters::network::http_connection_manager::v3::
                                HttpConnectionManager::KEEP_UNCHANGED));
  TestRequestHeaderMapImpl original_headers;
  original_headers.setPath("/xyz%2fabc%5Cqrt");

  TestRequestHeaderMapImpl header_map(original_headers);
  EXPECT_EQ(ConnectionManagerUtility::NormalizePathAction::Continue,
            ConnectionManagerUtility::maybeNormalizePath(header_map, config_));
  EXPECT_EQ(header_map.getPathValue(), "/xyz%2fabc%5Cqrt");
}

// maybeNormalizePath() returns REJECT if %2F or %5C was detected and configured to REJECT.
TEST_F(ConnectionManagerUtilityTest, RejectIfEscapedSlashesPresentAndConfiguredToReject) {
  ON_CALL(config_, pathWithEscapedSlashesAction())
      .WillByDefault(Return(envoy::extensions::filters::network::http_connection_manager::v3::
                                HttpConnectionManager::REJECT_REQUEST));
  TestRequestHeaderMapImpl original_headers;
  original_headers.setPath("/xyz%2F..//abc");

  TestRequestHeaderMapImpl header_map(original_headers);
  EXPECT_EQ(ConnectionManagerUtility::NormalizePathAction::Reject,
            ConnectionManagerUtility::maybeNormalizePath(header_map, config_));

  original_headers.setPath("/xyz%5c..//abc");
  header_map = original_headers;
  EXPECT_EQ(ConnectionManagerUtility::NormalizePathAction::Reject,
            ConnectionManagerUtility::maybeNormalizePath(header_map, config_));
}

// maybeNormalizePath() returns CONTINUE if escaped slashes were NOT present and configured to
// REJECT.
TEST_F(ConnectionManagerUtilityTest, RejectIfEscapedSlashesNotPresentAndConfiguredToReject) {
  ON_CALL(config_, pathWithEscapedSlashesAction())
      .WillByDefault(Return(envoy::extensions::filters::network::http_connection_manager::v3::
                                HttpConnectionManager::REJECT_REQUEST));
  TestRequestHeaderMapImpl original_headers;
  original_headers.setPath("/xyz%EA/abc");

  TestRequestHeaderMapImpl header_map(original_headers);
  EXPECT_EQ(ConnectionManagerUtility::NormalizePathAction::Continue,
            ConnectionManagerUtility::maybeNormalizePath(header_map, config_));
  EXPECT_EQ(header_map.getPathValue(), "/xyz%EA/abc");
}

// maybeNormalizePath() returns REDIRECT if escaped slashes were detected and configured to
// REDIRECT.
TEST_F(ConnectionManagerUtilityTest, RedirectIfEscapedSlashesPresentAndConfiguredToRedirect) {
  ON_CALL(config_, pathWithEscapedSlashesAction())
      .WillByDefault(Return(envoy::extensions::filters::network::http_connection_manager::v3::
                                HttpConnectionManager::UNESCAPE_AND_REDIRECT));
  TestRequestHeaderMapImpl original_headers;
  original_headers.setPath("/xyz%2F../%5cabc");

  TestRequestHeaderMapImpl header_map(original_headers);
  EXPECT_EQ(ConnectionManagerUtility::NormalizePathAction::Redirect,
            ConnectionManagerUtility::maybeNormalizePath(header_map, config_));
  EXPECT_EQ(header_map.getPathValue(), "/xyz/../\\abc");
}

// maybeNormalizePath() returns CONTINUE if escaped slashes were NOT present and configured to
// REDIRECT.
TEST_F(ConnectionManagerUtilityTest, ContinueIfEscapedSlashesNotFoundAndConfiguredToRedirect) {
  ON_CALL(config_, pathWithEscapedSlashesAction())
      .WillByDefault(Return(envoy::extensions::filters::network::http_connection_manager::v3::
                                HttpConnectionManager::UNESCAPE_AND_REDIRECT));
  TestRequestHeaderMapImpl original_headers;
  original_headers.setPath("/xyz%30..//abc");

  TestRequestHeaderMapImpl header_map(original_headers);
  EXPECT_EQ(ConnectionManagerUtility::NormalizePathAction::Continue,
            ConnectionManagerUtility::maybeNormalizePath(header_map, config_));
  EXPECT_EQ(header_map.getPathValue(), "/xyz%30..//abc");
}

// maybeNormalizePath() returns CONTINUE if escaped slashes were detected and configured to
// UNESCAPE_AND_FORWARD.
TEST_F(ConnectionManagerUtilityTest, ContinueIfEscapedSlashesPresentAndConfiguredToUnescape) {
  ON_CALL(config_, pathWithEscapedSlashesAction())
      .WillByDefault(Return(envoy::extensions::filters::network::http_connection_manager::v3::
                                HttpConnectionManager::UNESCAPE_AND_FORWARD));
  TestRequestHeaderMapImpl original_headers;
  original_headers.setPath("/xyz%2F../%5Cabc");

  TestRequestHeaderMapImpl header_map(original_headers);
  EXPECT_EQ(ConnectionManagerUtility::NormalizePathAction::Continue,
            ConnectionManagerUtility::maybeNormalizePath(header_map, config_));
  EXPECT_EQ(header_map.getPathValue(), "/xyz/../\\abc");
}

// maybeNormalizePath() performs both slash unescaping and Chromium URL normalization.
TEST_F(ConnectionManagerUtilityTest, UnescapeSlashesAndChromiumNormalization) {
  ON_CALL(config_, shouldNormalizePath()).WillByDefault(Return(true));
  ON_CALL(config_, pathWithEscapedSlashesAction())
      .WillByDefault(Return(envoy::extensions::filters::network::http_connection_manager::v3::
                                HttpConnectionManager::UNESCAPE_AND_FORWARD));
  TestRequestHeaderMapImpl original_headers;
  original_headers.setPath("/xyz%2f../%5Cabc");

  TestRequestHeaderMapImpl header_map(original_headers);
  EXPECT_EQ(ConnectionManagerUtility::NormalizePathAction::Continue,
            ConnectionManagerUtility::maybeNormalizePath(header_map, config_));
  // Chromium URL path normalization converts \ to /
  EXPECT_EQ(header_map.getPathValue(), "//abc");
}

// maybeNormalizePath() rejects request when chromium normalization fails after unescaping slashes.
TEST_F(ConnectionManagerUtilityTest, UnescapeSlashesRedirectAndChromiumNormalizationFailure) {
  ON_CALL(config_, shouldNormalizePath()).WillByDefault(Return(true));
  ON_CALL(config_, pathWithEscapedSlashesAction())
      .WillByDefault(Return(envoy::extensions::filters::network::http_connection_manager::v3::
                                HttpConnectionManager::UNESCAPE_AND_REDIRECT));
  TestRequestHeaderMapImpl original_headers;
  // %00 is an invalid sequence in URL path and causes path normalization to fail.
  original_headers.setPath("/xyz%2f../%5Cabc%00");

  TestRequestHeaderMapImpl header_map(original_headers);
  EXPECT_EQ(ConnectionManagerUtility::NormalizePathAction::Reject,
            ConnectionManagerUtility::maybeNormalizePath(header_map, config_));
}

// maybeNormalizePath() performs both unescaping and merging slashes when configured.
TEST_F(ConnectionManagerUtilityTest, UnescapeAndMergeSlashes) {
  ON_CALL(config_, shouldMergeSlashes()).WillByDefault(Return(true));
  ON_CALL(config_, pathWithEscapedSlashesAction())
      .WillByDefault(Return(envoy::extensions::filters::network::http_connection_manager::v3::
                                HttpConnectionManager::UNESCAPE_AND_REDIRECT));
  TestRequestHeaderMapImpl original_headers;
  original_headers.setPath("/xyz%2f/..//abc%5C%5c");

  TestRequestHeaderMapImpl header_map(original_headers);
  EXPECT_EQ(ConnectionManagerUtility::NormalizePathAction::Redirect,
            ConnectionManagerUtility::maybeNormalizePath(header_map, config_));
  // Envoy does not merge back slashes
  EXPECT_EQ(header_map.getPathValue(), "/xyz/../abc\\\\");
}

// maybeNormalizePath() performs all path transformations.
TEST_F(ConnectionManagerUtilityTest, AllNormalizations) {
  ON_CALL(config_, shouldNormalizePath()).WillByDefault(Return(true));
  ON_CALL(config_, shouldMergeSlashes()).WillByDefault(Return(true));
  ON_CALL(config_, pathWithEscapedSlashesAction())
      .WillByDefault(Return(envoy::extensions::filters::network::http_connection_manager::v3::
                                HttpConnectionManager::UNESCAPE_AND_FORWARD));
  TestRequestHeaderMapImpl original_headers;
  original_headers.setPath("/xyz%2f..%5c/%2Fabc");

  TestRequestHeaderMapImpl header_map(original_headers);
  EXPECT_EQ(ConnectionManagerUtility::NormalizePathAction::Continue,
            ConnectionManagerUtility::maybeNormalizePath(header_map, config_));
  EXPECT_EQ(header_map.getPathValue(), "/abc");
}

// maybeNormalizePath() redirects because of escaped slashes after all other transformations.
TEST_F(ConnectionManagerUtilityTest, RedirectAfterAllOtherNormalizations) {
  ON_CALL(config_, shouldNormalizePath()).WillByDefault(Return(true));
  ON_CALL(config_, shouldMergeSlashes()).WillByDefault(Return(true));
  ON_CALL(config_, pathWithEscapedSlashesAction())
      .WillByDefault(Return(envoy::extensions::filters::network::http_connection_manager::v3::
                                HttpConnectionManager::UNESCAPE_AND_REDIRECT));
  TestRequestHeaderMapImpl original_headers;
  original_headers.setPath("/xyz%2f..%5c/%2Fabc");

  TestRequestHeaderMapImpl header_map(original_headers);
  EXPECT_EQ(ConnectionManagerUtility::NormalizePathAction::Redirect,
            ConnectionManagerUtility::maybeNormalizePath(header_map, config_));
  EXPECT_EQ(header_map.getPathValue(), "/abc");
}

// test preserve_external_request_id true does not reset the passed requestId if passed
TEST_F(ConnectionManagerUtilityTest, PreserveExternalRequestId) {
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("134.2.2.11"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  ON_CALL(config_, preserveExternalRequestId()).WillByDefault(Return(true));
  TestRequestHeaderMapImpl headers{{"x-request-id", "my-request-id"},
                                   {"x-forwarded-for", "198.51.100.1"}};
  EXPECT_CALL(*request_id_extension_, set(testing::Ref(headers), false));
  EXPECT_CALL(*request_id_extension_, set(_, true)).Times(0);
  EXPECT_EQ((MutateRequestRet{"134.2.2.11:0", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_CALL(random_, uuid()).Times(0);
  EXPECT_EQ("my-request-id", headers.get_("x-request-id"));
}

// test preserve_external_request_id true but generates new request id when not passed
TEST_F(ConnectionManagerUtilityTest, PreseverExternalRequestIdNoReqId) {
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("134.2.2.11"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  ON_CALL(config_, preserveExternalRequestId()).WillByDefault(Return(true));
  TestRequestHeaderMapImpl headers{{"x-forwarded-for", "198.51.100.1"}};
  EXPECT_CALL(*request_id_extension_, set(testing::Ref(headers), false));
  EXPECT_CALL(*request_id_extension_, set(_, true)).Times(0);
  EXPECT_EQ((MutateRequestRet{"134.2.2.11:0", false, Tracing::Reason::NotTraceable}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ(random_.uuid_, headers.get_(Headers::get().RequestId));
}

// test preserve_external_request_id true and no edge_request passing requestId should keep the
// requestID
TEST_F(ConnectionManagerUtilityTest, PreserveExternalRequestIdNoEdgeRequestKeepRequestId) {
  ON_CALL(config_, preserveExternalRequestId()).WillByDefault(Return(true));
  TestRequestHeaderMapImpl headers{{"x-request-id", "myReqId"}};
  EXPECT_CALL(*request_id_extension_, set(testing::Ref(headers), false));
  EXPECT_CALL(*request_id_extension_, set(_, true)).Times(0);
  callMutateRequestHeaders(headers, Protocol::Http2);
  EXPECT_EQ("myReqId", headers.get_(Headers::get().RequestId));
}

// test preserve_external_request_id true and no edge_request not passing requestId should generate
// new request id
TEST_F(ConnectionManagerUtilityTest, PreserveExternalRequestIdNoEdgeRequestGenerateNewRequestId) {
  ON_CALL(config_, preserveExternalRequestId()).WillByDefault(Return(true));
  TestRequestHeaderMapImpl headers;
  EXPECT_CALL(*request_id_extension_, set(testing::Ref(headers), false));
  EXPECT_CALL(*request_id_extension_, set(_, true)).Times(0);
  callMutateRequestHeaders(headers, Protocol::Http2);
  EXPECT_EQ(random_.uuid_, headers.get_(Headers::get().RequestId));
}

// test preserve_external_request_id false edge request generates new request id
TEST_F(ConnectionManagerUtilityTest, NoPreserveExternalRequestIdEdgeRequestGenerateRequestId) {
  ON_CALL(config_, preserveExternalRequestId()).WillByDefault(Return(false));
  connection_.stream_info_.downstream_address_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("134.2.2.11"));

  // with request id
  {
    ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
    TestRequestHeaderMapImpl headers{{"x-forwarded-for", "198.51.100.1"},
                                     {"x-request-id", "my-request-id"}};
    EXPECT_CALL(*request_id_extension_, set(testing::Ref(headers), true));
    EXPECT_CALL(*request_id_extension_, set(_, false)).Times(0);
    EXPECT_EQ((MutateRequestRet{"134.2.2.11:0", false, Tracing::Reason::NotTraceable}),
              callMutateRequestHeaders(headers, Protocol::Http2));
    EXPECT_EQ(random_.uuid_, headers.get_(Headers::get().RequestId));
  }

  // with no request id
  {
    TestRequestHeaderMapImpl headers{{"x-forwarded-for", "198.51.100.1"}};
    EXPECT_CALL(*request_id_extension_, set(testing::Ref(headers), true));
    EXPECT_CALL(*request_id_extension_, set(_, false)).Times(0);
    EXPECT_EQ((MutateRequestRet{"134.2.2.11:0", false, Tracing::Reason::NotTraceable}),
              callMutateRequestHeaders(headers, Protocol::Http2));
    EXPECT_EQ(random_.uuid_, headers.get_(Headers::get().RequestId));
  }
}

// test preserve_external_request_id false not edge request
TEST_F(ConnectionManagerUtilityTest, NoPreserveExternalRequestIdNoEdgeRequest) {
  ON_CALL(config_, preserveExternalRequestId()).WillByDefault(Return(false));

  // with no request id
  {
    TestRequestHeaderMapImpl headers;
    EXPECT_CALL(*request_id_extension_, set(testing::Ref(headers), false));
    EXPECT_CALL(*request_id_extension_, set(_, true)).Times(0);
    callMutateRequestHeaders(headers, Protocol::Http2);
    EXPECT_EQ(random_.uuid_, headers.get_(Headers::get().RequestId));
  }

  // with request id
  {
    TestRequestHeaderMapImpl headers{{"x-request-id", "my-request-id"}};
    EXPECT_CALL(*request_id_extension_, set(testing::Ref(headers), false));
    EXPECT_CALL(*request_id_extension_, set(_, true)).Times(0);
    callMutateRequestHeaders(headers, Protocol::Http2);
    EXPECT_EQ("my-request-id", headers.get_(Headers::get().RequestId));
  }
}

// Test detecting the original IP via a header (no rejection if it fails).
TEST_F(ConnectionManagerUtilityTest, OriginalIPDetectionExtension) {
  const std::string header_name = "x-cdn-detected-ip";
  auto detection_extension = getCustomHeaderExtension(header_name);
  const std::vector<Http::OriginalIPDetectionSharedPtr> extensions = {detection_extension};

  ON_CALL(config_, originalIpDetectionExtensions()).WillByDefault(ReturnRef(extensions));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(false));

  // Header is present.
  {
    TestRequestHeaderMapImpl headers{{header_name, "2.1.3.4"}};
    auto ret = callMutateRequestHeaders(headers, Protocol::Http11);
    EXPECT_EQ(ret.downstream_address_, "2.1.3.4:0");
    EXPECT_EQ(ret.reject_request_, absl::nullopt);
  }

  // Header missing -- fallbacks to default behavior.
  {
    TestRequestHeaderMapImpl headers;
    auto ret = callMutateRequestHeaders(headers, Protocol::Http11);
    EXPECT_EQ(ret.downstream_address_, "10.0.0.3:50000");
    EXPECT_EQ(ret.reject_request_, absl::nullopt);
  }
}

} // namespace Http
} // namespace Envoy
