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

TEST_F(ConnectionManagerUtilityTest, UseRemoteAddressWhenNotLocalHostRemoteAddress) {
  Network::Address::Ipv4Instance not_local_host_remote_address("12.12.12.12");
  EXPECT_CALL(config_, useRemoteAddress()).WillRepeatedly(Return(true));
  EXPECT_CALL(connection_, remoteAddress())
      .WillRepeatedly(ReturnRef(not_local_host_remote_address));

  TestHeaderMapImpl headers{};
  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);

  EXPECT_TRUE(headers.has(Headers::get().ForwardedFor));
  EXPECT_EQ(not_local_host_remote_address.ip()->addressAsString(),
            headers.get_(Headers::get().ForwardedFor));
}

TEST_F(ConnectionManagerUtilityTest, UseLocalAddressWhenLocalHostRemoteAddress) {
  Network::Address::Ipv4Instance local_host_remote_address("127.0.0.1");
  Network::Address::Ipv4Instance local_address("10.3.2.1");

  EXPECT_CALL(connection_, remoteAddress()).WillRepeatedly(ReturnRef(local_host_remote_address));
  EXPECT_CALL(config_, useRemoteAddress()).WillRepeatedly(Return(true));
  EXPECT_CALL(config_, localAddress()).WillRepeatedly(ReturnRef(local_address));

  TestHeaderMapImpl headers{};
  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);

  EXPECT_TRUE(headers.has(Headers::get().ForwardedFor));
  EXPECT_EQ(local_address.ip()->addressAsString(), headers.get_(Headers::get().ForwardedFor));
}

TEST_F(ConnectionManagerUtilityTest, UserAgentDontSet) {
  Network::Address::Ipv4Instance internal_remote_address("10.0.0.1");

  EXPECT_CALL(config_, useRemoteAddress()).WillRepeatedly(Return(true));
  EXPECT_CALL(connection_, remoteAddress()).WillRepeatedly(ReturnRef(internal_remote_address));

  TestHeaderMapImpl headers{{"user-agent", "foo"}};
  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);

  EXPECT_EQ("foo", headers.get_(Headers::get().UserAgent));
  EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceCluster));
  EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceNode));
  EXPECT_EQ("true", headers.get_(Headers::get().EnvoyInternalRequest));
}

TEST_F(ConnectionManagerUtilityTest, UserAgentSetWhenIncomingEmpty) {
  Network::Address::Ipv4Instance internal_remote_address("10.0.0.1");

  EXPECT_CALL(config_, useRemoteAddress()).WillRepeatedly(Return(true));
  EXPECT_CALL(connection_, remoteAddress()).WillRepeatedly(ReturnRef(internal_remote_address));

  user_agent_.value("bar");
  TestHeaderMapImpl headers{{"user-agent", ""}, {"x-envoy-downstream-service-cluster", "foo"}};
  EXPECT_CALL(local_info_, nodeName()).Times(2).WillRepeatedly(Return(canary_node_));
  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);

  EXPECT_EQ("bar", headers.get_(Headers::get().UserAgent));
  EXPECT_EQ("bar", headers.get_(Headers::get().EnvoyDownstreamServiceCluster));
  EXPECT_EQ("canary", headers.get_(Headers::get().EnvoyDownstreamServiceNode));
  EXPECT_EQ("true", headers.get_(Headers::get().EnvoyInternalRequest));
}

TEST_F(ConnectionManagerUtilityTest, InternalServiceForceTrace) {
  const std::string uuid = "f4dca0a9-12c7-4307-8002-969403baf480";

  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(false));

  {
    // Internal request, make traceable
    TestHeaderMapImpl headers{
        {"x-forwarded-for", "10.0.0.1"}, {"x-request-id", uuid}, {"x-envoy-force-trace", "true"}};
    EXPECT_CALL(random_, uuid()).Times(0);
    EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(true));
    ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                   route_config_, random_, runtime_, local_info_);

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
    ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                   route_config_, random_, runtime_, local_info_);
    EXPECT_EQ(uuid, headers.get_(Headers::get().RequestId));
    EXPECT_FALSE(headers.has(Headers::get().EnvoyForceTrace));
  }
}

TEST_F(ConnectionManagerUtilityTest, EdgeRequestRegenerateRequestIdAndWipeDownstream) {
  Network::Address::Ipv4Instance external_remote_address("34.0.0.1");

  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  ON_CALL(connection_, remoteAddress()).WillByDefault(ReturnRef(external_remote_address));
  ON_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
      .WillByDefault(Return(true));

  {
    TestHeaderMapImpl headers{{"x-envoy-downstream-service-cluster", "foo"},
                              {"x-request-id", "will_be_regenerated"}};
    EXPECT_CALL(random_, uuid());

    EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.client_enabled", _)).Times(0);
    ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                   route_config_, random_, runtime_, local_info_);

    EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceCluster));
    EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceNode));
    // No changes to uuid as x-client-trace-id is missing.
    EXPECT_EQ(random_.uuid_, headers.get_(Headers::get().RequestId));
  }

  {
    // Runtime does not allow to make request traceable even though x-client-request-id set
    TestHeaderMapImpl headers{{"x-envoy-downstream-service-cluster", "foo"},
                              {"x-request-id", "will_be_regenerated"},
                              {"x-client-trace-id", "trace-id"}};
    EXPECT_CALL(random_, uuid());
    EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.client_enabled", 100))
        .WillOnce(Return(false));

    ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                   route_config_, random_, runtime_, local_info_);

    EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceCluster));
    EXPECT_EQ(random_.uuid_, headers.get_(Headers::get().RequestId));
  }

  {
    // Runtime is enabled for tracing and x-client-request-id set

    TestHeaderMapImpl headers{{"x-envoy-downstream-service-cluster", "foo"},
                              {"x-request-id", "will_be_regenerated"},
                              {"x-client-trace-id", "trace-id"}};
    EXPECT_CALL(random_, uuid());
    EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.client_enabled", 100))
        .WillOnce(Return(true));

    ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                   route_config_, random_, runtime_, local_info_);

    EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceCluster));
    // Traceable (client trace) variant of random_.uuid_
    EXPECT_EQ("a121e9e1-feae-b136-9e0e-6fac343d56c9", headers.get_(Headers::get().RequestId));
  }
}

TEST_F(ConnectionManagerUtilityTest, ExternalRequestPreserveRequestIdAndDownstream) {
  EXPECT_CALL(config_, useRemoteAddress()).WillRepeatedly(Return(false));
  EXPECT_CALL(connection_, remoteAddress()).Times(0);
  TestHeaderMapImpl headers{{"x-envoy-downstream-service-cluster", "foo"},
                            {"x-request-id", "id"},
                            {"x-forwarded-for", "34.0.0.1"}};

  EXPECT_CALL(local_info_, nodeName()).Times(0);
  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);

  EXPECT_EQ("foo", headers.get_(Headers::get().EnvoyDownstreamServiceCluster));
  EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceNode));
  EXPECT_EQ("id", headers.get_(Headers::get().RequestId));
  EXPECT_FALSE(headers.has(Headers::get().EnvoyInternalRequest));
}

TEST_F(ConnectionManagerUtilityTest, UserAgentSetIncomingUserAgent) {
  Network::Address::Ipv4Instance internal_remote_address("10.0.0.1");

  EXPECT_CALL(config_, useRemoteAddress()).WillRepeatedly(Return(true));
  EXPECT_CALL(connection_, remoteAddress()).WillRepeatedly(ReturnRef(internal_remote_address));

  user_agent_.value("bar");
  TestHeaderMapImpl headers{{"user-agent", "foo"}, {"x-envoy-downstream-service-cluster", "foo"}};
  EXPECT_CALL(local_info_, nodeName()).WillOnce(Return(empty_node_));
  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);

  EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceNode));
  EXPECT_EQ("foo", headers.get_(Headers::get().UserAgent));
  EXPECT_EQ("bar", headers.get_(Headers::get().EnvoyDownstreamServiceCluster));
  EXPECT_EQ("true", headers.get_(Headers::get().EnvoyInternalRequest));
}

TEST_F(ConnectionManagerUtilityTest, UserAgentSetNoIncomingUserAgent) {
  Network::Address::Ipv4Instance internal_remote_address("10.0.0.1");

  EXPECT_CALL(config_, useRemoteAddress()).WillRepeatedly(Return(true));
  EXPECT_CALL(connection_, remoteAddress()).WillRepeatedly(ReturnRef(internal_remote_address));

  user_agent_.value("bar");
  TestHeaderMapImpl headers{};
  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);

  EXPECT_TRUE(headers.has(Headers::get().UserAgent));
  EXPECT_EQ("bar", headers.get_(Headers::get().UserAgent));
  EXPECT_EQ("bar", headers.get_(Headers::get().EnvoyDownstreamServiceCluster));
  EXPECT_EQ("true", headers.get_(Headers::get().EnvoyInternalRequest));
}

TEST_F(ConnectionManagerUtilityTest, RequestIdGeneratedWhenItsNotPresent) {
  {
    TestHeaderMapImpl headers{{":authority", "host"}, {":path", "/"}};
    EXPECT_CALL(random_, uuid()).WillOnce(Return("generated_uuid"));

    ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                   route_config_, random_, runtime_, local_info_);
    EXPECT_EQ("generated_uuid", headers.get_("x-request-id"));
  }

  {
    Runtime::RandomGeneratorImpl rand;
    TestHeaderMapImpl headers{{"x-client-trace-id", "trace-id"}};
    std::string uuid = rand.uuid();

    EXPECT_CALL(random_, uuid()).WillOnce(Return(uuid));

    ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                   route_config_, random_, runtime_, local_info_);
    // x-request-id should not be set to be traceable as it's not edge request
    EXPECT_EQ(uuid, headers.get_("x-request-id"));
  }
}

TEST_F(ConnectionManagerUtilityTest, DoNotOverrideRequestIdIfPresentWhenInternalRequest) {
  Network::Address::Ipv4Instance local_remote_address("10.0.0.1");
  EXPECT_CALL(config_, useRemoteAddress()).WillOnce(Return(true));
  EXPECT_CALL(connection_, remoteAddress()).WillRepeatedly(ReturnRef(local_remote_address));

  TestHeaderMapImpl headers{{"x-request-id", "original_request_id"}};
  EXPECT_CALL(random_, uuid()).Times(0);

  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);
  EXPECT_EQ("original_request_id", headers.get_("x-request-id"));
}

TEST_F(ConnectionManagerUtilityTest, OverrideRequestIdForExternalRequests) {
  Network::Address::Ipv4Instance external_ip("134.2.2.11");
  EXPECT_CALL(connection_, remoteAddress()).WillRepeatedly(ReturnRef(external_ip));
  TestHeaderMapImpl headers{{"x-request-id", "original"}};

  EXPECT_CALL(random_, uuid()).WillOnce(Return("override"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));

  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);
  EXPECT_EQ("override", headers.get_("x-request-id"));
}

TEST_F(ConnectionManagerUtilityTest, ExternalAddressExternalRequestUseRemote) {
  Network::Address::Ipv4Instance external_ip("50.0.0.1");
  ON_CALL(connection_, remoteAddress()).WillByDefault(ReturnRef(external_ip));
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
  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);
  EXPECT_EQ("50.0.0.1", headers.get_("x-envoy-external-address"));
  EXPECT_FALSE(headers.has("x-envoy-internal"));
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

TEST_F(ConnectionManagerUtilityTest, ExternalAddressExternalRequestDontUseRemote) {
  Network::Address::Ipv4Instance external_ip("60.0.0.1");
  ON_CALL(connection_, remoteAddress()).WillByDefault(ReturnRef(external_ip));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(false));

  TestHeaderMapImpl headers{{"x-envoy-external-address", "60.0.0.1"},
                            {"x-forwarded-for", "60.0.0.1"}};
  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);
  EXPECT_EQ("60.0.0.1", headers.get_("x-envoy-external-address"));
  EXPECT_EQ("60.0.0.1", headers.get_("x-forwarded-for"));
  EXPECT_FALSE(headers.has("x-envoy-internal"));
}

TEST_F(ConnectionManagerUtilityTest, ExternalAddressInternalRequestUseRemote) {
  Network::Address::Ipv4Instance local_remote_address("10.0.0.1");
  ON_CALL(connection_, remoteAddress()).WillByDefault(ReturnRef(local_remote_address));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));

  TestHeaderMapImpl headers{{"x-envoy-external-address", "60.0.0.1"},
                            {"x-envoy-expected-rq-timeout-ms", "10"}};
  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);
  EXPECT_EQ("60.0.0.1", headers.get_("x-envoy-external-address"));
  EXPECT_EQ("10.0.0.1", headers.get_("x-forwarded-for"));
  EXPECT_EQ("10", headers.get_("x-envoy-expected-rq-timeout-ms"));
  EXPECT_TRUE(headers.has("x-envoy-internal"));
}

TEST_F(ConnectionManagerUtilityTest, DoNotRemoveConnectionUpgradeForWebSocketRequests) {
  TestHeaderMapImpl headers{{"connection", "upgrade"}, {"upgrade", "websocket"}};

  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http11, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);
  EXPECT_EQ("upgrade", headers.get_("connection"));
  EXPECT_EQ("websocket", headers.get_("upgrade"));
}

TEST_F(ConnectionManagerUtilityTest, RemoveConnectionUpgradeForNonWebSocketRequests) {
  TestHeaderMapImpl headers{{"connection", "close"}, {"upgrade", "websocket"}};

  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http11, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);
  EXPECT_FALSE(headers.has("connection"));
  EXPECT_FALSE(headers.has("upgrade"));
}

TEST_F(ConnectionManagerUtilityTest, RemoveConnectionUpgradeForHttp2Requests) {
  TestHeaderMapImpl headers{{"connection", "upgrade"}, {"upgrade", "websocket"}};

  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);
  EXPECT_FALSE(headers.has("connection"));
  EXPECT_FALSE(headers.has("upgrade"));
}

TEST_F(ConnectionManagerUtilityTest, MutateResponseHeaders) {
  TestHeaderMapImpl response_headers{
      {"connection", "foo"}, {"transfer-encoding", "foo"}, {"custom_header", "foo"}};
  TestHeaderMapImpl request_headers{{"x-request-id", "request-id"}};

  ConnectionManagerUtility::mutateResponseHeaders(response_headers, request_headers);

  EXPECT_EQ(1UL, response_headers.size());
  EXPECT_EQ("foo", response_headers.get_("custom_header"));
  EXPECT_FALSE(response_headers.has("x-request-id"));
}

TEST_F(ConnectionManagerUtilityTest, MutateResponseHeadersReturnXRequestId) {
  TestHeaderMapImpl response_headers;
  TestHeaderMapImpl request_headers{{"x-request-id", "request-id"},
                                    {"x-envoy-force-trace", "true"}};

  ConnectionManagerUtility::mutateResponseHeaders(response_headers, request_headers);
  EXPECT_EQ("request-id", response_headers.get_("x-request-id"));
}

TEST_F(ConnectionManagerUtilityTest, MtlsSanitizeClientCert) {
  NiceMock<Ssl::MockConnection> ssl;
  ON_CALL(ssl, peerCertificatePresented()).WillByDefault(Return(true));
  ON_CALL(connection_, ssl()).WillByDefault(Return(&ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::Sanitize));
  std::vector<Http::ClientCertDetailsType> details;
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));

  TestHeaderMapImpl headers{{"x-forwarded-client-cert", "By=test;SAN=abc"}};
  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);
  EXPECT_FALSE(headers.has("x-forwarded-client-cert"));
}

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
  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/fe;SAN=test://bar.com/be", headers.get_("x-forwarded-client-cert"));
}

TEST_F(ConnectionManagerUtilityTest, MtlsAppendForwardClientCert) {
  // This test assumes the following scenario:
  // The client identity is foo.com/fe, and the server (local) dentity is foo.com/be. The client
  // also sends the XFCC
  // header with the authentication result of the previous hop, (bar.com/be calling foo.com/fe).
  NiceMock<Ssl::MockConnection> ssl;
  ON_CALL(ssl, peerCertificatePresented()).WillByDefault(Return(true));
  EXPECT_CALL(ssl, uriSanLocalCertificate()).Times(2).WillRepeatedly(Return("test://foo.com/be"));
  EXPECT_CALL(ssl, sha256PeerCertificateDigest()).Times(2).WillRepeatedly(Return("abcdefg"));
  EXPECT_CALL(ssl, uriSanPeerCertificate()).WillOnce(Return("test://foo.com/fe"));
  ON_CALL(connection_, ssl()).WillByDefault(Return(&ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::AppendForward));
  std::vector<Http::ClientCertDetailsType> details = std::vector<Http::ClientCertDetailsType>();
  details.push_back(Http::ClientCertDetailsType::SAN);
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));

  TestHeaderMapImpl headers{
      {"x-forwarded-client-cert", "By=test://foo.com/fe;SAN=test://bar.com/be"}};
  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/fe;SAN=test://bar.com/be,By=test://foo.com/"
            "be;Hash=abcdefg;SAN=test://foo.com/fe",
            headers.get_("x-forwarded-client-cert"));
}

TEST_F(ConnectionManagerUtilityTest, MtlsAppendForwardClientCertLocalSanEmpty) {
  // This test assumes the following scenario:
  // The client identity is foo.com/fe, and the server (local) dentity is foo.com/be. The client
  // also sends the XFCC
  // header with the authentication result of the previous hop, (bar.com/be calling foo.com/fe).
  NiceMock<Ssl::MockConnection> ssl;
  ON_CALL(ssl, peerCertificatePresented()).WillByDefault(Return(true));
  EXPECT_CALL(ssl, uriSanLocalCertificate()).WillOnce(Return(""));
  EXPECT_CALL(ssl, sha256PeerCertificateDigest()).Times(2).WillRepeatedly(Return("abcdefg"));
  EXPECT_CALL(ssl, uriSanPeerCertificate()).WillOnce(Return("test://foo.com/fe"));
  ON_CALL(connection_, ssl()).WillByDefault(Return(&ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::AppendForward));
  std::vector<Http::ClientCertDetailsType> details = std::vector<Http::ClientCertDetailsType>();
  details.push_back(Http::ClientCertDetailsType::SAN);
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));

  TestHeaderMapImpl headers{
      {"x-forwarded-client-cert", "By=test://foo.com/fe;Hash=xyz;SAN=test://bar.com/be"}};
  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/fe;Hash=xyz;SAN=test://bar.com/be,"
            "Hash=abcdefg;SAN=test://foo.com/fe",
            headers.get_("x-forwarded-client-cert"));
}

TEST_F(ConnectionManagerUtilityTest, MtlsSanitizeSetClientCert) {
  // This test assumes the following scenario:
  // The client identity is foo.com/fe, and the server (local) dentity is foo.com/be. The client
  // also sends the XFCC header with the authentication result of the previous hop, (bar.com/be
  // calling foo.com/fe).
  NiceMock<Ssl::MockConnection> ssl;
  ON_CALL(ssl, peerCertificatePresented()).WillByDefault(Return(true));
  EXPECT_CALL(ssl, uriSanLocalCertificate()).Times(2).WillRepeatedly(Return("test://foo.com/be"));
  EXPECT_CALL(ssl, sha256PeerCertificateDigest()).Times(2).WillRepeatedly(Return("abcdefg"));
  EXPECT_CALL(ssl, subjectPeerCertificate())
      .WillOnce(Return("/C=US/ST=CA/L=San Francisco/OU=Lyft/CN=test.lyft.com"));
  EXPECT_CALL(ssl, uriSanPeerCertificate()).WillOnce(Return("test://foo.com/fe"));
  ON_CALL(connection_, ssl()).WillByDefault(Return(&ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::SanitizeSet));
  std::vector<Http::ClientCertDetailsType> details = std::vector<Http::ClientCertDetailsType>();
  details.push_back(Http::ClientCertDetailsType::Subject);
  details.push_back(Http::ClientCertDetailsType::SAN);
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));

  TestHeaderMapImpl headers{
      {"x-forwarded-client-cert", "By=test://foo.com/fe;SAN=test://bar.com/be"}};
  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/be;Hash=abcdefg;Subject=\"/C=US/ST=CA/L=San "
            "Francisco/OU=Lyft/CN=test.lyft.com\";SAN=test://foo.com/fe",
            headers.get_("x-forwarded-client-cert"));
}

TEST_F(ConnectionManagerUtilityTest, MtlsSanitizeSetClientCertPeerSanEmpty) {
  // This test assumes the following scenario:
  // The client identity is foo.com/fe, and the server (local) dentity is foo.com/be. The client
  // also sends the XFCC header with the authentication result of the previous hop, (bar.com/be
  // calling foo.com/fe).
  NiceMock<Ssl::MockConnection> ssl;
  ON_CALL(ssl, peerCertificatePresented()).WillByDefault(Return(true));
  EXPECT_CALL(ssl, uriSanLocalCertificate()).Times(2).WillRepeatedly(Return("test://foo.com/be"));
  EXPECT_CALL(ssl, sha256PeerCertificateDigest()).Times(2).WillRepeatedly(Return("abcdefg"));
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
  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/be;Hash=abcdefg;Subject=\"/C=US/ST=CA/L=San "
            "Francisco/OU=Lyft/CN=test.lyft.com\";SAN=",
            headers.get_("x-forwarded-client-cert"));
}

TEST_F(ConnectionManagerUtilityTest, TlsSanitizeClientCertWhenForward) {
  // forward_only, append_forward and sanitize_set are only effective in mTLS connection.
  NiceMock<Ssl::MockConnection> ssl;
  ON_CALL(ssl, peerCertificatePresented()).WillByDefault(Return(false));
  ON_CALL(connection_, ssl()).WillByDefault(Return(&ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::ForwardOnly));
  std::vector<Http::ClientCertDetailsType> details;
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));

  TestHeaderMapImpl headers{{"x-forwarded-client-cert", "By=test;SAN=abc"}};
  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);
  EXPECT_FALSE(headers.has("x-forwarded-client-cert"));
}

TEST_F(ConnectionManagerUtilityTest, TlsAlwaysForwardOnlyClientCert) {
  // always_forward_only works regardless whether the connection is TLS/mTLS.
  NiceMock<Ssl::MockConnection> ssl;
  ON_CALL(ssl, peerCertificatePresented()).WillByDefault(Return(false));
  ON_CALL(connection_, ssl()).WillByDefault(Return(&ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::AlwaysForwardOnly));
  std::vector<Http::ClientCertDetailsType> details;
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));

  TestHeaderMapImpl headers{
      {"x-forwarded-client-cert", "By=test://foo.com/fe;SAN=test://bar.com/be"}};
  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/fe;SAN=test://bar.com/be", headers.get_("x-forwarded-client-cert"));
}

TEST_F(ConnectionManagerUtilityTest, NonTlsSanitizeClientCertWhenForward) {
  // forward_only, append_forward and sanitize_set are only effective in mTLS connection.
  ON_CALL(connection_, ssl()).WillByDefault(Return(nullptr));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::ForwardOnly));
  std::vector<Http::ClientCertDetailsType> details;
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));

  TestHeaderMapImpl headers{{"x-forwarded-client-cert", "By=test;SAN=abc"}};
  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);
  EXPECT_FALSE(headers.has("x-forwarded-client-cert"));
}

TEST_F(ConnectionManagerUtilityTest, NonTlsAlwaysForwardClientCert) {
  // always_forward_only works regardless whether the connection is TLS/mTLS.
  ON_CALL(connection_, ssl()).WillByDefault(Return(nullptr));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::AlwaysForwardOnly));
  std::vector<Http::ClientCertDetailsType> details;
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));

  TestHeaderMapImpl headers{
      {"x-forwarded-client-cert", "By=test://foo.com/fe;SAN=test://bar.com/be"}};
  ConnectionManagerUtility::mutateRequestHeaders(headers, Protocol::Http2, connection_, config_,
                                                 route_config_, random_, runtime_, local_info_);
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/fe;SAN=test://bar.com/be", headers.get_("x-forwarded-client-cert"));
}

} // namespace Http
} // namespace Envoy
