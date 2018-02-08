#include <string>

#include "common/http/conn_manager_utility.h"
#include "common/http/headers.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"
#include "common/runtime/runtime_impl.h"
#include "common/runtime/uuid_util.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace Http {

class ConnectionManagerUtilityTest : public testing::Test {
public:
  ConnectionManagerUtilityTest() {
    ON_CALL(config_, userAgent()).WillByDefault(ReturnRef(user_agent_));

    tracing_config_ = {Tracing::OperationName::Ingress, {}};
    ON_CALL(config_, tracingConfig()).WillByDefault(Return(&tracing_config_));
  }

  struct MutateRequestRet {
    bool operator==(const MutateRequestRet& rhs) const {
      return downstream_address_ == rhs.downstream_address_ && internal_ == rhs.internal_;
    }

    std::string downstream_address_;
    bool internal_;
  };

  // This is a convenience method used to call mutateRequestHeaders(). It is done in this
  // convoluted way to force tests to check both the final downstream address as well as whether
  // the request is internal/external, given the importance of these two pieces of data.
  MutateRequestRet callMutateRequestHeaders(HeaderMap& headers, Protocol protocol) {
    MutateRequestRet ret;
    ret.downstream_address_ =
        ConnectionManagerUtility::mutateRequestHeaders(
            headers, protocol, connection_, config_, route_config_, random_, runtime_, local_info_)
            ->asString();
    ret.internal_ = headers.EnvoyInternalRequest() != nullptr;
    return ret;
  }

  NiceMock<Network::MockConnection> connection_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<MockConnectionManagerConfig> config_;
  NiceMock<Router::MockConfig> route_config_;
  Optional<std::string> user_agent_;
  NiceMock<Runtime::MockLoader> runtime_;
  Http::TracingConnectionManagerConfig tracing_config_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  std::string canary_node_{"canary"};
  std::string empty_node_{""};
};

// Verify external request and XFF is set when we are using remote address and the address is
// external.
TEST_F(ConnectionManagerUtilityTest, UseRemoteAddressWhenNotLocalHostRemoteAddress) {
  connection_.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("12.12.12.12");
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  TestHeaderMapImpl headers;

  EXPECT_EQ((MutateRequestRet{"12.12.12.12:0", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ(connection_.remote_address_->ip()->addressAsString(),
            headers.get_(Headers::get().ForwardedFor));
}

// Verify internal request and XFF is set when we are using remote address the address is internal.
TEST_F(ConnectionManagerUtilityTest, UseRemoteAddressWhenLocalHostRemoteAddress) {
  connection_.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1");
  Network::Address::Ipv4Instance local_address("10.3.2.1");
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  ON_CALL(config_, localAddress()).WillByDefault(ReturnRef(local_address));
  TestHeaderMapImpl headers;

  EXPECT_EQ((MutateRequestRet{"127.0.0.1:0", true}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ(local_address.ip()->addressAsString(), headers.get_(Headers::get().ForwardedFor));
}

// Verify that we trust Nth address from XFF when using remote address with xff_num_trusted_hops.
TEST_F(ConnectionManagerUtilityTest, UseRemoteAddressWithXFFTrustedHops) {
  connection_.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("203.0.113.128");
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  ON_CALL(config_, xffNumTrustedHops()).WillByDefault(Return(1));
  TestHeaderMapImpl headers{{"x-forwarded-for", "198.51.100.1"}};
  EXPECT_EQ((MutateRequestRet{"198.51.100.1:0", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ(headers.EnvoyExternalAddress()->value(), "198.51.100.1");
}

// Verify that xff_num_trusted_hops works when not using remote address.
TEST_F(ConnectionManagerUtilityTest, UseXFFTrustedHopsWithoutRemoteAddress) {
  connection_.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1");
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(false));
  ON_CALL(config_, xffNumTrustedHops()).WillByDefault(Return(1));
  TestHeaderMapImpl headers{{"x-forwarded-for", "198.51.100.2, 198.51.100.1"}};
  EXPECT_EQ((MutateRequestRet{"198.51.100.2:0", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ(headers.EnvoyExternalAddress(), nullptr);
}

// Verify that we don't set user agent when it is already set.
TEST_F(ConnectionManagerUtilityTest, UserAgentDontSet) {
  connection_.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1");
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  TestHeaderMapImpl headers{{"user-agent", "foo"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.1:0", true}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ("foo", headers.get_(Headers::get().UserAgent));
  EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceCluster));
  EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceNode));
}

// Verify that we do set user agent when it is empty.
TEST_F(ConnectionManagerUtilityTest, UserAgentSetWhenIncomingEmpty) {
  connection_.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1");
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  ON_CALL(local_info_, nodeName()).WillByDefault(Return(canary_node_));
  user_agent_.value("bar");
  TestHeaderMapImpl headers{{"user-agent", ""}, {"x-envoy-downstream-service-cluster", "foo"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.1:0", true}),
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
    TestHeaderMapImpl headers{
        {"x-forwarded-for", "10.0.0.1"}, {"x-request-id", uuid}, {"x-envoy-force-trace", "true"}};
    EXPECT_CALL(random_, uuid()).Times(0);
    EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(true));

    EXPECT_EQ((MutateRequestRet{"10.0.0.1:0", true}),
              callMutateRequestHeaders(headers, Protocol::Http2));
    // Traceable (forced trace) variant of uuid
    EXPECT_EQ("f4dca0a9-12c7-a307-8002-969403baf480", headers.get_(Headers::get().RequestId));
  }

  {
    // Not internal request, force trace header should be cleaned.
    TestHeaderMapImpl headers{
        {"x-forwarded-for", "34.0.0.1"}, {"x-request-id", uuid}, {"x-envoy-force-trace", "true"}};
    EXPECT_CALL(random_, uuid()).Times(0);
    EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(true));

    EXPECT_EQ((MutateRequestRet{"34.0.0.1:0", false}),
              callMutateRequestHeaders(headers, Protocol::Http2));
    EXPECT_EQ(uuid, headers.get_(Headers::get().RequestId));
    EXPECT_FALSE(headers.has(Headers::get().EnvoyForceTrace));
  }
}

// Test generating request-id in various edge request scenarios.
TEST_F(ConnectionManagerUtilityTest, EdgeRequestRegenerateRequestIdAndWipeDownstream) {
  connection_.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("34.0.0.1");
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  ON_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
      .WillByDefault(Return(true));

  {
    TestHeaderMapImpl headers{{"x-envoy-downstream-service-cluster", "foo"},
                              {"x-request-id", "will_be_regenerated"}};
    EXPECT_CALL(random_, uuid());

    EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.client_enabled", _)).Times(0);
    EXPECT_EQ((MutateRequestRet{"34.0.0.1:0", false}),
              callMutateRequestHeaders(headers, Protocol::Http2));
    EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceCluster));
    EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceNode));
    // No changes to uuid as x-client-trace-id is missing.
    EXPECT_EQ(random_.uuid_, headers.get_(Headers::get().RequestId));
  }

  {
    // Runtime does not allow to make request traceable even though x-client-trace-id set.
    TestHeaderMapImpl headers{{"x-envoy-downstream-service-cluster", "foo"},
                              {"x-request-id", "will_be_regenerated"},
                              {"x-client-trace-id", "trace-id"}};
    EXPECT_CALL(random_, uuid());
    EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.client_enabled", 100))
        .WillOnce(Return(false));

    EXPECT_EQ((MutateRequestRet{"34.0.0.1:0", false}),
              callMutateRequestHeaders(headers, Protocol::Http2));
    EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceCluster));
    EXPECT_EQ(random_.uuid_, headers.get_(Headers::get().RequestId));
  }

  {
    // Runtime is enabled for tracing and x-client-trace-id set.
    TestHeaderMapImpl headers{{"x-envoy-downstream-service-cluster", "foo"},
                              {"x-request-id", "will_be_regenerated"},
                              {"x-client-trace-id", "trace-id"}};
    EXPECT_CALL(random_, uuid());
    EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.client_enabled", 100))
        .WillOnce(Return(true));

    EXPECT_EQ((MutateRequestRet{"34.0.0.1:0", false}),
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
  EXPECT_CALL(connection_, remoteAddress()).Times(0);
  TestHeaderMapImpl headers{{"x-envoy-downstream-service-cluster", "foo"},
                            {"x-request-id", "id"},
                            {"x-forwarded-for", "34.0.0.1"}};

  EXPECT_CALL(local_info_, nodeName()).Times(0);

  EXPECT_EQ((MutateRequestRet{"34.0.0.1:0", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ("foo", headers.get_(Headers::get().EnvoyDownstreamServiceCluster));
  EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceNode));
  EXPECT_EQ("id", headers.get_(Headers::get().RequestId));
}

// Verify that we don't overwrite user agent, but do set x-envoy-downstream-service-cluster
// correctly.
TEST_F(ConnectionManagerUtilityTest, UserAgentSetIncomingUserAgent) {
  connection_.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1");
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));

  user_agent_.value("bar");
  TestHeaderMapImpl headers{{"user-agent", "foo"}, {"x-envoy-downstream-service-cluster", "foo"}};
  EXPECT_CALL(local_info_, nodeName()).WillOnce(Return(empty_node_));

  EXPECT_EQ((MutateRequestRet{"10.0.0.1:0", true}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceNode));
  EXPECT_EQ("foo", headers.get_(Headers::get().UserAgent));
  EXPECT_EQ("bar", headers.get_(Headers::get().EnvoyDownstreamServiceCluster));
}

// Verify that we set both user agent and x-envoy-downstream-service-cluster.
TEST_F(ConnectionManagerUtilityTest, UserAgentSetNoIncomingUserAgent) {
  connection_.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1");
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  user_agent_.value("bar");
  TestHeaderMapImpl headers;

  EXPECT_EQ((MutateRequestRet{"10.0.0.1:0", true}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ("bar", headers.get_(Headers::get().UserAgent));
  EXPECT_EQ("bar", headers.get_(Headers::get().EnvoyDownstreamServiceCluster));
}

// Test different permutations of request-id generation.
TEST_F(ConnectionManagerUtilityTest, RequestIdGeneratedWhenItsNotPresent) {
  {
    TestHeaderMapImpl headers{{":authority", "host"}, {":path", "/"}};
    EXPECT_CALL(random_, uuid()).WillOnce(Return("generated_uuid"));

    EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false}),
              callMutateRequestHeaders(headers, Protocol::Http2));
    EXPECT_EQ("generated_uuid", headers.get_("x-request-id"));
  }

  {
    Runtime::RandomGeneratorImpl rand;
    TestHeaderMapImpl headers{{"x-client-trace-id", "trace-id"}};
    const std::string uuid = rand.uuid();
    EXPECT_CALL(random_, uuid()).WillOnce(Return(uuid));

    EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false}),
              callMutateRequestHeaders(headers, Protocol::Http2));
    // x-request-id should not be set to be traceable as it's not edge request
    EXPECT_EQ(uuid, headers.get_("x-request-id"));
  }
}

// Make sure we do not overwrite x-request-id if the request is internal.
TEST_F(ConnectionManagerUtilityTest, DoNotOverrideRequestIdIfPresentWhenInternalRequest) {
  connection_.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1");
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  TestHeaderMapImpl headers{{"x-request-id", "original_request_id"}};
  EXPECT_CALL(random_, uuid()).Times(0);

  EXPECT_EQ((MutateRequestRet{"10.0.0.1:0", true}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ("original_request_id", headers.get_("x-request-id"));
}

// Make sure that we do overwrite x-request-id for "edge" external requests.
TEST_F(ConnectionManagerUtilityTest, OverrideRequestIdForExternalRequests) {
  connection_.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("134.2.2.11");
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  TestHeaderMapImpl headers{{"x-request-id", "original"}};
  EXPECT_CALL(random_, uuid()).WillOnce(Return("override"));

  EXPECT_EQ((MutateRequestRet{"134.2.2.11:0", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ("override", headers.get_("x-request-id"));
}

// A request that uses remote address and is from an external address should be treated as an
// external request with all internal only headers cleaned.
TEST_F(ConnectionManagerUtilityTest, ExternalAddressExternalRequestUseRemote) {
  connection_.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("50.0.0.1");
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  route_config_.internal_only_headers_.push_back(LowerCaseString("custom_header"));
  TestHeaderMapImpl headers{{"x-envoy-decorator-operation", "foo"},
                            {"x-envoy-downstream-service-cluster", "foo"},
                            {"x-envoy-retry-on", "foo"},
                            {"x-envoy-retry-grpc-on", "foo"},
                            {"x-envoy-max-retries", "foo"},
                            {"x-envoy-upstream-alt-stat-name", "foo"},
                            {"x-envoy-upstream-rq-timeout-alt-response", "204"},
                            {"x-envoy-upstream-rq-timeout-ms", "foo"},
                            {"x-envoy-expected-rq-timeout-ms", "10"},
                            {"custom_header", "foo"}};

  EXPECT_EQ((MutateRequestRet{"50.0.0.1:0", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ("50.0.0.1", headers.get_("x-envoy-external-address"));
  EXPECT_FALSE(headers.has("x-envoy-decorator-operation"));
  EXPECT_FALSE(headers.has("x-envoy-downstream-service-cluster"));
  EXPECT_FALSE(headers.has("x-envoy-retry-on"));
  EXPECT_FALSE(headers.has("x-envoy-retry-grpc-on"));
  EXPECT_FALSE(headers.has("x-envoy-max-retries"));
  EXPECT_FALSE(headers.has("x-envoy-upstream-alt-stat-name"));
  EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-alt-response"));
  EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
  EXPECT_FALSE(headers.has("x-envoy-expected-rq-timeout-ms"));
  EXPECT_FALSE(headers.has("custom_header"));
}

// A request that is from an external address, but does not use remote address, should pull the
// address from XFF.
TEST_F(ConnectionManagerUtilityTest, ExternalAddressExternalRequestDontUseRemote) {
  connection_.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("60.0.0.2");
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(false));
  TestHeaderMapImpl headers{{"x-envoy-external-address", "60.0.0.1"},
                            {"x-forwarded-for", "60.0.0.1"}};

  EXPECT_EQ((MutateRequestRet{"60.0.0.1:0", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ("60.0.0.1", headers.get_("x-envoy-external-address"));
  EXPECT_EQ("60.0.0.1", headers.get_("x-forwarded-for"));
}

// Verify that if XFF is invalid we fall back to remote address, even if it is a pipe.
TEST_F(ConnectionManagerUtilityTest, PipeAddressInvalidXFFtDontUseRemote) {
  connection_.remote_address_ = std::make_shared<Network::Address::PipeInstance>("/blah");
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(false));
  TestHeaderMapImpl headers{{"x-forwarded-for", "blah"}};

  EXPECT_EQ((MutateRequestRet{"/blah", false}), callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_FALSE(headers.has("x-envoy-external-address"));
}

// Verify that we treat a request as external even if the direct remote is internal and XFF
// includes only internal addresses. Note that this is legacy behavior. See the comments
// in mutateRequestHeaders() for more info.
TEST_F(ConnectionManagerUtilityTest, AppendInternalAddressXffNotInternalRequest) {
  connection_.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1");
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  TestHeaderMapImpl headers{{"x-forwarded-for", "10.0.0.2"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.1:0", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ("10.0.0.2, 10.0.0.1", headers.get_("x-forwarded-for"));
}

// A request that is from an internal address and uses remote address should be an internal request.
// It should also preserve x-envoy-external-address.
TEST_F(ConnectionManagerUtilityTest, ExternalAddressInternalRequestUseRemote) {
  connection_.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1");
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  TestHeaderMapImpl headers{{"x-envoy-external-address", "60.0.0.1"},
                            {"x-envoy-expected-rq-timeout-ms", "10"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.1:0", true}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ("60.0.0.1", headers.get_("x-envoy-external-address"));
  EXPECT_EQ("10.0.0.1", headers.get_("x-forwarded-for"));
  EXPECT_EQ("10", headers.get_("x-envoy-expected-rq-timeout-ms"));
}

// Make sure we don't remove connection headers for WS requests.
TEST_F(ConnectionManagerUtilityTest, DoNotRemoveConnectionUpgradeForWebSocketRequests) {
  TestHeaderMapImpl headers{{"connection", "upgrade"}, {"upgrade", "websocket"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false}),
            callMutateRequestHeaders(headers, Protocol::Http11));
  EXPECT_EQ("upgrade", headers.get_("connection"));
  EXPECT_EQ("websocket", headers.get_("upgrade"));
}

// Make sure we do remove connection headers for non-WS requests.
TEST_F(ConnectionManagerUtilityTest, RemoveConnectionUpgradeForNonWebSocketRequests) {
  TestHeaderMapImpl headers{{"connection", "close"}, {"upgrade", "websocket"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false}),
            callMutateRequestHeaders(headers, Protocol::Http11));
  EXPECT_FALSE(headers.has("connection"));
  EXPECT_FALSE(headers.has("upgrade"));
}

// Make sure we remove connections headers for a WS request over h2.
TEST_F(ConnectionManagerUtilityTest, RemoveConnectionUpgradeForHttp2Requests) {
  TestHeaderMapImpl headers{{"connection", "upgrade"}, {"upgrade", "websocket"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_FALSE(headers.has("connection"));
  EXPECT_FALSE(headers.has("upgrade"));
}

// Test cleaning response headers.
TEST_F(ConnectionManagerUtilityTest, MutateResponseHeaders) {
  TestHeaderMapImpl response_headers{
      {"connection", "foo"}, {"transfer-encoding", "foo"}, {"custom_header", "foo"}};
  TestHeaderMapImpl request_headers{{"x-request-id", "request-id"}};

  ConnectionManagerUtility::mutateResponseHeaders(response_headers, request_headers);

  EXPECT_EQ(1UL, response_headers.size());
  EXPECT_EQ("foo", response_headers.get_("custom_header"));
  EXPECT_FALSE(response_headers.has("x-request-id"));
}

// Test that we correctly return x-request-id if we were requested to force a trace.
TEST_F(ConnectionManagerUtilityTest, MutateResponseHeadersReturnXRequestId) {
  TestHeaderMapImpl response_headers;
  TestHeaderMapImpl request_headers{{"x-request-id", "request-id"},
                                    {"x-envoy-force-trace", "true"}};

  ConnectionManagerUtility::mutateResponseHeaders(response_headers, request_headers);
  EXPECT_EQ("request-id", response_headers.get_("x-request-id"));
}

// Test full sanitization of x-forwarded-client-cert.
TEST_F(ConnectionManagerUtilityTest, MtlsSanitizeClientCert) {
  NiceMock<Ssl::MockConnection> ssl;
  ON_CALL(ssl, peerCertificatePresented()).WillByDefault(Return(true));
  ON_CALL(connection_, ssl()).WillByDefault(Return(&ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::Sanitize));
  std::vector<Http::ClientCertDetailsType> details;
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestHeaderMapImpl headers{{"x-forwarded-client-cert", "By=test;SAN=abc"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_FALSE(headers.has("x-forwarded-client-cert"));
}

// Test that we sanitize and set x-forwarded-client-cert.
TEST_F(ConnectionManagerUtilityTest, MtlsForwardOnlyClientCert) {
  NiceMock<Ssl::MockConnection> ssl;
  ON_CALL(ssl, peerCertificatePresented()).WillByDefault(Return(true));
  ON_CALL(connection_, ssl()).WillByDefault(Return(&ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::ForwardOnly));
  std::vector<Http::ClientCertDetailsType> details;
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestHeaderMapImpl headers{
      {"x-forwarded-client-cert", "By=test://foo.com/fe;SAN=test://bar.com/be"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/fe;SAN=test://bar.com/be", headers.get_("x-forwarded-client-cert"));
}

// This test assumes the following scenario:
// The client identity is foo.com/fe, and the server (local) dentity is foo.com/be. The client
// also sends the XFCC header with the authentication result of the previous hop, (bar.com/be
// calling foo.com/fe).
TEST_F(ConnectionManagerUtilityTest, MtlsAppendForwardClientCert) {
  NiceMock<Ssl::MockConnection> ssl;
  ON_CALL(ssl, peerCertificatePresented()).WillByDefault(Return(true));
  EXPECT_CALL(ssl, uriSanLocalCertificate()).WillOnce(Return("test://foo.com/be"));
  std::string expected_sha("abcdefg");
  EXPECT_CALL(ssl, sha256PeerCertificateDigest()).WillOnce(ReturnRef(expected_sha));
  EXPECT_CALL(ssl, uriSanPeerCertificate()).WillOnce(Return("test://foo.com/fe"));
  std::string expected_pem("%3D%3Dabc%0Ade%3D");
  EXPECT_CALL(ssl, urlEncodedPemEncodedPeerCertificate()).WillOnce(ReturnRef(expected_pem));
  ON_CALL(connection_, ssl()).WillByDefault(Return(&ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::AppendForward));
  std::vector<Http::ClientCertDetailsType> details = std::vector<Http::ClientCertDetailsType>();
  details.push_back(Http::ClientCertDetailsType::SAN);
  details.push_back(Http::ClientCertDetailsType::Cert);
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestHeaderMapImpl headers{
      {"x-forwarded-client-cert", "By=test://foo.com/fe;SAN=test://bar.com/be"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/fe;SAN=test://bar.com/be,By=test://foo.com/"
            "be;Hash=abcdefg;SAN=test://foo.com/fe;Cert=\"%3D%3Dabc%0Ade%3D\"",
            headers.get_("x-forwarded-client-cert"));
}

// This test assumes the following scenario:
// The client identity is foo.com/fe, and the server (local) dentity is foo.com/be. The client
// also sends the XFCC header with the authentication result of the previous hop, (bar.com/be
// calling foo.com/fe).
TEST_F(ConnectionManagerUtilityTest, MtlsAppendForwardClientCertLocalSanEmpty) {
  NiceMock<Ssl::MockConnection> ssl;
  ON_CALL(ssl, peerCertificatePresented()).WillByDefault(Return(true));
  EXPECT_CALL(ssl, uriSanLocalCertificate()).WillOnce(Return(""));
  std::string expected_sha("abcdefg");
  EXPECT_CALL(ssl, sha256PeerCertificateDigest()).WillOnce(ReturnRef(expected_sha));
  EXPECT_CALL(ssl, uriSanPeerCertificate()).WillOnce(Return("test://foo.com/fe"));
  ON_CALL(connection_, ssl()).WillByDefault(Return(&ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::AppendForward));
  std::vector<Http::ClientCertDetailsType> details = std::vector<Http::ClientCertDetailsType>();
  details.push_back(Http::ClientCertDetailsType::SAN);
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestHeaderMapImpl headers{
      {"x-forwarded-client-cert", "By=test://foo.com/fe;Hash=xyz;SAN=test://bar.com/be"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/fe;Hash=xyz;SAN=test://bar.com/be,"
            "Hash=abcdefg;SAN=test://foo.com/fe",
            headers.get_("x-forwarded-client-cert"));
}

// This test assumes the following scenario:
// The client identity is foo.com/fe, and the server (local) dentity is foo.com/be. The client
// also sends the XFCC header with the authentication result of the previous hop, (bar.com/be
// calling foo.com/fe).
TEST_F(ConnectionManagerUtilityTest, MtlsSanitizeSetClientCert) {
  NiceMock<Ssl::MockConnection> ssl;
  ON_CALL(ssl, peerCertificatePresented()).WillByDefault(Return(true));
  EXPECT_CALL(ssl, uriSanLocalCertificate()).WillOnce(Return("test://foo.com/be"));
  std::string expected_sha("abcdefg");
  EXPECT_CALL(ssl, sha256PeerCertificateDigest()).WillOnce(ReturnRef(expected_sha));
  EXPECT_CALL(ssl, subjectPeerCertificate())
      .WillOnce(Return("/C=US/ST=CA/L=San Francisco/OU=Lyft/CN=test.lyft.com"));
  EXPECT_CALL(ssl, uriSanPeerCertificate()).WillOnce(Return("test://foo.com/fe"));
  std::string expected_pem("abcde=");
  EXPECT_CALL(ssl, urlEncodedPemEncodedPeerCertificate()).WillOnce(ReturnRef(expected_pem));
  ON_CALL(connection_, ssl()).WillByDefault(Return(&ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::SanitizeSet));
  std::vector<Http::ClientCertDetailsType> details = std::vector<Http::ClientCertDetailsType>();
  details.push_back(Http::ClientCertDetailsType::Subject);
  details.push_back(Http::ClientCertDetailsType::SAN);
  details.push_back(Http::ClientCertDetailsType::Cert);
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestHeaderMapImpl headers{
      {"x-forwarded-client-cert", "By=test://foo.com/fe;SAN=test://bar.com/be"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/be;Hash=abcdefg;Subject=\"/C=US/ST=CA/L=San "
            "Francisco/OU=Lyft/CN=test.lyft.com\";SAN=test://foo.com/fe;Cert=\"abcde=\"",
            headers.get_("x-forwarded-client-cert"));
}

// This test assumes the following scenario:
// The client identity is foo.com/fe, and the server (local) dentity is foo.com/be. The client
// also sends the XFCC header with the authentication result of the previous hop, (bar.com/be
// calling foo.com/fe).
TEST_F(ConnectionManagerUtilityTest, MtlsSanitizeSetClientCertPeerSanEmpty) {
  NiceMock<Ssl::MockConnection> ssl;
  ON_CALL(ssl, peerCertificatePresented()).WillByDefault(Return(true));
  EXPECT_CALL(ssl, uriSanLocalCertificate()).WillOnce(Return("test://foo.com/be"));
  std::string expected_sha("abcdefg");
  EXPECT_CALL(ssl, sha256PeerCertificateDigest()).WillOnce(ReturnRef(expected_sha));
  EXPECT_CALL(ssl, subjectPeerCertificate())
      .WillOnce(Return("/C=US/ST=CA/L=San Francisco/OU=Lyft/CN=test.lyft.com"));
  EXPECT_CALL(ssl, uriSanPeerCertificate()).WillOnce(Return(""));
  ON_CALL(connection_, ssl()).WillByDefault(Return(&ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::SanitizeSet));
  std::vector<Http::ClientCertDetailsType> details = std::vector<Http::ClientCertDetailsType>();
  details.push_back(Http::ClientCertDetailsType::Subject);
  details.push_back(Http::ClientCertDetailsType::SAN);
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestHeaderMapImpl headers{
      {"x-forwarded-client-cert", "By=test://foo.com/fe;SAN=test://bar.com/be"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/be;Hash=abcdefg;Subject=\"/C=US/ST=CA/L=San "
            "Francisco/OU=Lyft/CN=test.lyft.com\";SAN=",
            headers.get_("x-forwarded-client-cert"));
}

// forward_only, append_forward and sanitize_set are only effective in mTLS connection.
TEST_F(ConnectionManagerUtilityTest, TlsSanitizeClientCertWhenForward) {
  NiceMock<Ssl::MockConnection> ssl;
  ON_CALL(ssl, peerCertificatePresented()).WillByDefault(Return(false));
  ON_CALL(connection_, ssl()).WillByDefault(Return(&ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::ForwardOnly));
  std::vector<Http::ClientCertDetailsType> details;
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestHeaderMapImpl headers{{"x-forwarded-client-cert", "By=test;SAN=abc"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_FALSE(headers.has("x-forwarded-client-cert"));
}

// always_forward_only works regardless whether the connection is TLS/mTLS.
TEST_F(ConnectionManagerUtilityTest, TlsAlwaysForwardOnlyClientCert) {
  NiceMock<Ssl::MockConnection> ssl;
  ON_CALL(ssl, peerCertificatePresented()).WillByDefault(Return(false));
  ON_CALL(connection_, ssl()).WillByDefault(Return(&ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::AlwaysForwardOnly));
  std::vector<Http::ClientCertDetailsType> details;
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestHeaderMapImpl headers{
      {"x-forwarded-client-cert", "By=test://foo.com/fe;SAN=test://bar.com/be"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/fe;SAN=test://bar.com/be", headers.get_("x-forwarded-client-cert"));
}

// forward_only, append_forward and sanitize_set are only effective in mTLS connection.
TEST_F(ConnectionManagerUtilityTest, NonTlsSanitizeClientCertWhenForward) {
  ON_CALL(connection_, ssl()).WillByDefault(Return(nullptr));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::ForwardOnly));
  std::vector<Http::ClientCertDetailsType> details;
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestHeaderMapImpl headers{{"x-forwarded-client-cert", "By=test;SAN=abc"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false}),
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
  TestHeaderMapImpl headers{
      {"x-forwarded-client-cert", "By=test://foo.com/fe;SAN=test://bar.com/be"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/fe;SAN=test://bar.com/be", headers.get_("x-forwarded-client-cert"));
}

} // namespace Http
} // namespace Envoy
