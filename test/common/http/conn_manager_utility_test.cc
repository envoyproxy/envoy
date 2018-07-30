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

class MockConnectionManagerConfig : public ConnectionManagerConfig {
public:
  MockConnectionManagerConfig() { ON_CALL(*this, generateRequestId()).WillByDefault(Return(true)); }

  // Http::ConnectionManagerConfig
  ServerConnectionPtr createCodec(Network::Connection& connection, const Buffer::Instance& instance,
                                  ServerConnectionCallbacks& callbacks) override {
    return ServerConnectionPtr{createCodec_(connection, instance, callbacks)};
  }

  MOCK_METHOD0(accessLogs, const std::list<AccessLog::InstanceSharedPtr>&());
  MOCK_METHOD3(createCodec_, ServerConnection*(Network::Connection&, const Buffer::Instance&,
                                               ServerConnectionCallbacks&));
  MOCK_METHOD0(dateProvider, DateProvider&());
  MOCK_METHOD0(drainTimeout, std::chrono::milliseconds());
  MOCK_METHOD0(filterFactory, FilterChainFactory&());
  MOCK_METHOD0(generateRequestId, bool());
  MOCK_CONST_METHOD0(idleTimeout, absl::optional<std::chrono::milliseconds>());
  MOCK_CONST_METHOD0(streamIdleTimeout, std::chrono::milliseconds());
  MOCK_METHOD0(routeConfigProvider, Router::RouteConfigProvider&());
  MOCK_METHOD0(serverName, const std::string&());
  MOCK_METHOD0(stats, ConnectionManagerStats&());
  MOCK_METHOD0(tracingStats, ConnectionManagerTracingStats&());
  MOCK_METHOD0(useRemoteAddress, bool());
  MOCK_CONST_METHOD0(xffNumTrustedHops, uint32_t());
  MOCK_CONST_METHOD0(skipXffAppend, bool());
  MOCK_CONST_METHOD0(via, const std::string&());
  MOCK_METHOD0(forwardClientCert, Http::ForwardClientCertType());
  MOCK_CONST_METHOD0(setCurrentClientCertDetails,
                     const std::vector<Http::ClientCertDetailsType>&());
  MOCK_METHOD0(localAddress, const Network::Address::Instance&());
  MOCK_METHOD0(userAgent, const absl::optional<std::string>&());
  MOCK_METHOD0(tracingConfig, const Http::TracingConnectionManagerConfig*());
  MOCK_METHOD0(listenerStats, ConnectionManagerListenerStats&());
  MOCK_CONST_METHOD0(proxy100Continue, bool());
  MOCK_CONST_METHOD0(http1Settings, const Http::Http1Settings&());
};

class ConnectionManagerUtilityTest : public testing::Test {
public:
  ConnectionManagerUtilityTest() {
    ON_CALL(config_, userAgent()).WillByDefault(ReturnRef(user_agent_));

    tracing_config_ = {Tracing::OperationName::Ingress, {}, 100, 10000, 100};
    ON_CALL(config_, tracingConfig()).WillByDefault(Return(&tracing_config_));

    ON_CALL(config_, via()).WillByDefault(ReturnRef(via_));
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
  absl::optional<std::string> user_agent_;
  NiceMock<Runtime::MockLoader> runtime_;
  Http::TracingConnectionManagerConfig tracing_config_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  std::string canary_node_{"canary"};
  std::string empty_node_;
  std::string via_;
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

// Verify that we don't append XFF when skipXffAppend(), even if using remote
// address and where the address is external.
TEST_F(ConnectionManagerUtilityTest, SkipXffAppendUseRemoteAddress) {
  EXPECT_CALL(config_, skipXffAppend()).WillOnce(Return(true));
  connection_.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("12.12.12.12");
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  TestHeaderMapImpl headers;

  EXPECT_EQ((MutateRequestRet{"12.12.12.12:0", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_FALSE(headers.has(Headers::get().ForwardedFor));
}

// Verify that we pass-thru XFF when skipAffAppend(), even if using remote
// address and where the address is external.
TEST_F(ConnectionManagerUtilityTest, SkipXffAppendPassThruUseRemoteAddress) {
  EXPECT_CALL(config_, skipXffAppend()).WillOnce(Return(true));
  connection_.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("12.12.12.12");
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  TestHeaderMapImpl headers{{"x-forwarded-for", "198.51.100.1"}};

  EXPECT_EQ((MutateRequestRet{"12.12.12.12:0", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_STREQ("198.51.100.1", headers.ForwardedFor()->value().c_str());
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

// Verify that we don't set the via header on requests/responses when empty.
TEST_F(ConnectionManagerUtilityTest, ViaEmpty) {
  connection_.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1");
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));

  TestHeaderMapImpl request_headers;
  EXPECT_EQ((MutateRequestRet{"10.0.0.1:0", true}),
            callMutateRequestHeaders(request_headers, Protocol::Http2));
  EXPECT_FALSE(request_headers.has(Headers::get().Via));

  TestHeaderMapImpl response_headers;
  ConnectionManagerUtility::mutateResponseHeaders(response_headers, &request_headers, via_);
  EXPECT_FALSE(response_headers.has(Headers::get().Via));
}

// Verify that we append a non-empty via header on requests/responses.
TEST_F(ConnectionManagerUtilityTest, ViaAppend) {
  via_ = "foo";
  connection_.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1");
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));

  TestHeaderMapImpl request_headers;
  EXPECT_EQ((MutateRequestRet{"10.0.0.1:0", true}),
            callMutateRequestHeaders(request_headers, Protocol::Http2));
  EXPECT_EQ("foo", request_headers.get_(Headers::get().Via));

  TestHeaderMapImpl response_headers;
  // Pretend we're doing a 100-continue transform here.
  ConnectionManagerUtility::mutateResponseHeaders(response_headers, &request_headers, "");
  // The actual response header processing.
  ConnectionManagerUtility::mutateResponseHeaders(response_headers, &request_headers, via_);
  EXPECT_EQ("foo", response_headers.get_(Headers::get().Via));
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
  user_agent_ = "bar";
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

  user_agent_ = "bar";
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
  user_agent_ = "bar";
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
                            {"x-envoy-ip-tags", "bar"},
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
  EXPECT_FALSE(headers.has("x-envoy-ip-tags"));
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
      {"connection", "foo"}, {"transfer-encoding", "foo"}, {"custom_header", "custom_value"}};
  TestHeaderMapImpl request_headers{{"x-request-id", "request-id"}};

  ConnectionManagerUtility::mutateResponseHeaders(response_headers, &request_headers, "");

  EXPECT_EQ(1UL, response_headers.size());
  EXPECT_EQ("custom_value", response_headers.get_("custom_header"));
  EXPECT_FALSE(response_headers.has("x-request-id"));
  EXPECT_FALSE(response_headers.has(Headers::get().Via));
}

// Make sure we don't remove connection headers on all Upgrade responses.
TEST_F(ConnectionManagerUtilityTest, DoNotRemoveConnectionUpgradeForWebSocketResponses) {
  TestHeaderMapImpl request_headers{{"connection", "UpGrAdE"}, {"upgrade", "foo"}};
  TestHeaderMapImpl response_headers{
      {"connection", "upgrade"}, {"transfer-encoding", "foo"}, {"upgrade", "bar"}};
  EXPECT_TRUE(Utility::isUpgrade(request_headers));
  EXPECT_TRUE(Utility::isUpgrade(response_headers));
  ConnectionManagerUtility::mutateResponseHeaders(response_headers, &request_headers, "");

  EXPECT_EQ(2UL, response_headers.size()) << response_headers;
  EXPECT_EQ("upgrade", response_headers.get_("connection"));
  EXPECT_EQ("bar", response_headers.get_("upgrade"));
}

TEST_F(ConnectionManagerUtilityTest, ClearUpgradeHeadersForNonUpgradeRequests) {
  // Test clearing non-upgrade request and response headers
  {
    TestHeaderMapImpl request_headers{{"x-request-id", "request-id"}};
    TestHeaderMapImpl response_headers{
        {"connection", "foo"}, {"transfer-encoding", "bar"}, {"custom_header", "custom_value"}};
    EXPECT_FALSE(Utility::isUpgrade(request_headers));
    EXPECT_FALSE(Utility::isUpgrade(response_headers));
    ConnectionManagerUtility::mutateResponseHeaders(response_headers, &request_headers, "");

    EXPECT_EQ(1UL, response_headers.size()) << response_headers;
    EXPECT_EQ("custom_value", response_headers.get_("custom_header"));
  }

  // Test with the request headers not valid upgrade headers
  {
    TestHeaderMapImpl request_headers{{"upgrade", "foo"}};
    TestHeaderMapImpl response_headers{{"connection", "upgrade"},
                                       {"transfer-encoding", "eep"},
                                       {"upgrade", "foo"},
                                       {"custom_header", "custom_value"}};
    EXPECT_FALSE(Utility::isUpgrade(request_headers));
    EXPECT_TRUE(Utility::isUpgrade(response_headers));
    ConnectionManagerUtility::mutateResponseHeaders(response_headers, &request_headers, "");

    EXPECT_EQ(2UL, response_headers.size()) << response_headers;
    EXPECT_EQ("custom_value", response_headers.get_("custom_header"));
    EXPECT_EQ("foo", response_headers.get_("upgrade"));
  }

  // Test with the response headers not valid upgrade headers
  {
    TestHeaderMapImpl request_headers{{"connection", "UpGrAdE"}, {"upgrade", "foo"}};
    TestHeaderMapImpl response_headers{{"transfer-encoding", "foo"}, {"upgrade", "bar"}};
    EXPECT_TRUE(Utility::isUpgrade(request_headers));
    EXPECT_FALSE(Utility::isUpgrade(response_headers));
    ConnectionManagerUtility::mutateResponseHeaders(response_headers, &request_headers, "");

    EXPECT_EQ(1UL, response_headers.size()) << response_headers;
    EXPECT_EQ("bar", response_headers.get_("upgrade"));
  }
}

// Test that we correctly return x-request-id if we were requested to force a trace.
TEST_F(ConnectionManagerUtilityTest, MutateResponseHeadersReturnXRequestId) {
  TestHeaderMapImpl response_headers;
  TestHeaderMapImpl request_headers{{"x-request-id", "request-id"},
                                    {"x-envoy-force-trace", "true"}};

  ConnectionManagerUtility::mutateResponseHeaders(response_headers, &request_headers, "");
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
  TestHeaderMapImpl headers{{"x-forwarded-client-cert", "By=test;URI=abc;DNS=example.com"}};

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
      {"x-forwarded-client-cert", "By=test://foo.com/fe;URI=test://bar.com/be;DNS=example.com"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/fe;URI=test://bar.com/be;DNS=example.com",
            headers.get_("x-forwarded-client-cert"));
}

// The server (local) dentity is foo.com/be. The client does not set XFCC.
TEST_F(ConnectionManagerUtilityTest, MtlsSetForwardClientCert) {
  NiceMock<Ssl::MockConnection> ssl;
  ON_CALL(ssl, peerCertificatePresented()).WillByDefault(Return(true));
  EXPECT_CALL(ssl, uriSanLocalCertificate()).WillOnce(Return("test://foo.com/be"));
  std::string expected_sha("abcdefg");
  EXPECT_CALL(ssl, sha256PeerCertificateDigest()).WillOnce(ReturnRef(expected_sha));
  EXPECT_CALL(ssl, uriSanPeerCertificate()).WillRepeatedly(Return("test://foo.com/fe"));
  std::string expected_pem("%3D%3Dabc%0Ade%3D");
  EXPECT_CALL(ssl, urlEncodedPemEncodedPeerCertificate()).WillOnce(ReturnRef(expected_pem));
  std::vector<std::string> expected_dns = {"www.example.com"};
  EXPECT_CALL(ssl, dnsSansPeerCertificate()).WillOnce(Return(expected_dns));
  ON_CALL(connection_, ssl()).WillByDefault(Return(&ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::AppendForward));
  std::vector<Http::ClientCertDetailsType> details = std::vector<Http::ClientCertDetailsType>();
  details.push_back(Http::ClientCertDetailsType::URI);
  details.push_back(Http::ClientCertDetailsType::Cert);
  details.push_back(Http::ClientCertDetailsType::DNS);
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestHeaderMapImpl headers;

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/be;"
            "Hash=abcdefg;"
            "URI=test://foo.com/fe;"
            "Cert=\"%3D%3Dabc%0Ade%3D\";"
            "DNS=www.example.com",
            headers.get_("x-forwarded-client-cert"));
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
  EXPECT_CALL(ssl, uriSanPeerCertificate()).WillRepeatedly(Return("test://foo.com/fe"));
  std::string expected_pem("%3D%3Dabc%0Ade%3D");
  EXPECT_CALL(ssl, urlEncodedPemEncodedPeerCertificate()).WillOnce(ReturnRef(expected_pem));
  std::vector<std::string> expected_dns = {"www.example.com"};
  EXPECT_CALL(ssl, dnsSansPeerCertificate()).WillOnce(Return(expected_dns));
  ON_CALL(connection_, ssl()).WillByDefault(Return(&ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::AppendForward));
  std::vector<Http::ClientCertDetailsType> details = std::vector<Http::ClientCertDetailsType>();
  details.push_back(Http::ClientCertDetailsType::URI);
  details.push_back(Http::ClientCertDetailsType::Cert);
  details.push_back(Http::ClientCertDetailsType::DNS);
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestHeaderMapImpl headers{{"x-forwarded-client-cert", "By=test://foo.com/fe;"
                                                        "URI=test://bar.com/be;"
                                                        "DNS=test.com;DNS=test.com"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/fe;URI=test://bar.com/be;DNS=test.com;DNS=test.com,"
            "By=test://foo.com/be;Hash=abcdefg;URI=test://foo.com/fe;"
            "Cert=\"%3D%3Dabc%0Ade%3D\";DNS=www.example.com",
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
  EXPECT_CALL(ssl, uriSanPeerCertificate()).WillRepeatedly(Return("test://foo.com/fe"));
  ON_CALL(connection_, ssl()).WillByDefault(Return(&ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::AppendForward));
  std::vector<Http::ClientCertDetailsType> details = std::vector<Http::ClientCertDetailsType>();
  details.push_back(Http::ClientCertDetailsType::URI);
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestHeaderMapImpl headers{
      {"x-forwarded-client-cert", "By=test://foo.com/fe;Hash=xyz;URI=test://bar.com/be"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/fe;Hash=xyz;URI=test://bar.com/be,"
            "Hash=abcdefg;URI=test://foo.com/fe",
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
  EXPECT_CALL(ssl, uriSanPeerCertificate()).WillRepeatedly(Return("test://foo.com/fe"));
  std::string expected_pem("abcde=");
  EXPECT_CALL(ssl, urlEncodedPemEncodedPeerCertificate()).WillOnce(ReturnRef(expected_pem));
  ON_CALL(connection_, ssl()).WillByDefault(Return(&ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::SanitizeSet));
  std::vector<Http::ClientCertDetailsType> details = std::vector<Http::ClientCertDetailsType>();
  details.push_back(Http::ClientCertDetailsType::Subject);
  details.push_back(Http::ClientCertDetailsType::URI);
  details.push_back(Http::ClientCertDetailsType::Cert);
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestHeaderMapImpl headers{
      {"x-forwarded-client-cert", "By=test://foo.com/fe;URI=test://bar.com/be"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/be;Hash=abcdefg;Subject=\"/C=US/ST=CA/L=San "
            "Francisco/OU=Lyft/CN=test.lyft.com\";URI=test://foo.com/"
            "fe;Cert=\"abcde=\"",
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
  EXPECT_CALL(ssl, uriSanPeerCertificate()).WillRepeatedly(Return(""));
  ON_CALL(connection_, ssl()).WillByDefault(Return(&ssl));
  ON_CALL(config_, forwardClientCert())
      .WillByDefault(Return(Http::ForwardClientCertType::SanitizeSet));
  std::vector<Http::ClientCertDetailsType> details = std::vector<Http::ClientCertDetailsType>();
  details.push_back(Http::ClientCertDetailsType::Subject);
  details.push_back(Http::ClientCertDetailsType::URI);
  ON_CALL(config_, setCurrentClientCertDetails()).WillByDefault(ReturnRef(details));
  TestHeaderMapImpl headers{
      {"x-forwarded-client-cert", "By=test://foo.com/fe;URI=test://bar.com/be"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/be;Hash=abcdefg;Subject=\"/C=US/ST=CA/L=San "
            "Francisco/OU=Lyft/CN=test.lyft.com\";URI=",
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
  TestHeaderMapImpl headers{{"x-forwarded-client-cert", "By=test;URI=abc"}};

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
      {"x-forwarded-client-cert", "By=test://foo.com/fe;URI=test://bar.com/be"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false}),
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
  TestHeaderMapImpl headers{{"x-forwarded-client-cert", "By=test;URI=abc"}};

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
      {"x-forwarded-client-cert", "By=test://foo.com/fe;URI=test://bar.com/be"}};

  EXPECT_EQ((MutateRequestRet{"10.0.0.3:50000", false}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_TRUE(headers.has("x-forwarded-client-cert"));
  EXPECT_EQ("By=test://foo.com/fe;URI=test://bar.com/be", headers.get_("x-forwarded-client-cert"));
}

// Sampling, global on.
TEST_F(ConnectionManagerUtilityTest, RandomSamplingWhenGlobalSet) {
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.random_sampling", 10000, _, 10000))
      .WillOnce(Return(true));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
      .WillOnce(Return(true));

  Http::TestHeaderMapImpl request_headers{{"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"}};
  callMutateRequestHeaders(request_headers, Protocol::Http2);

  EXPECT_EQ(UuidTraceStatus::Sampled,
            UuidUtils::isTraceableUuid(request_headers.get_("x-request-id")));
}

// Sampling must not be done on client traced.
TEST_F(ConnectionManagerUtilityTest, SamplingMustNotBeDoneOnClientTraced) {
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.random_sampling", 10000, _, 10000))
      .Times(0);
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
      .WillOnce(Return(true));

  // The x_request_id has TRACE_FORCED(a) set in the TRACE_BYTE_POSITION(14) character.
  Http::TestHeaderMapImpl request_headers{{"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}};
  callMutateRequestHeaders(request_headers, Protocol::Http2);

  EXPECT_EQ(UuidTraceStatus::Forced,
            UuidUtils::isTraceableUuid(request_headers.get_("x-request-id")));
}

// Sampling, global off.
TEST_F(ConnectionManagerUtilityTest, NoTraceWhenSamplingSetButGlobalNotSet) {
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.random_sampling", 10000, _, 10000))
      .WillOnce(Return(true));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
      .WillOnce(Return(false));

  Http::TestHeaderMapImpl request_headers{{"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"}};
  callMutateRequestHeaders(request_headers, Protocol::Http2);

  EXPECT_EQ(UuidTraceStatus::NoTrace,
            UuidUtils::isTraceableUuid(request_headers.get_("x-request-id")));
}

// Client, client enabled, global on.
TEST_F(ConnectionManagerUtilityTest, ClientSamplingWhenGlobalSet) {
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.client_enabled", 100))
      .WillOnce(Return(true));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
      .WillOnce(Return(true));

  Http::TestHeaderMapImpl request_headers{
      {"x-client-trace-id", "f4dca0a9-12c7-4307-8002-969403baf480"},
      {"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"}};
  callMutateRequestHeaders(request_headers, Protocol::Http2);

  EXPECT_EQ(UuidTraceStatus::Client,
            UuidUtils::isTraceableUuid(request_headers.get_("x-request-id")));
}

// Client, client disabled, global on.
TEST_F(ConnectionManagerUtilityTest, NoTraceWhenClientSamplingNotSetAndGlobalSet) {
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.client_enabled", 100))
      .WillOnce(Return(false));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
      .WillOnce(Return(true));

  Http::TestHeaderMapImpl request_headers{
      {"x-client-trace-id", "f4dca0a9-12c7-4307-8002-969403baf480"},
      {"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"}};
  callMutateRequestHeaders(request_headers, Protocol::Http2);

  EXPECT_EQ(UuidTraceStatus::NoTrace,
            UuidUtils::isTraceableUuid(request_headers.get_("x-request-id")));
}

// Forced, global on.
TEST_F(ConnectionManagerUtilityTest, ForcedTracedWhenGlobalSet) {
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(false));
  // Internal request, make traceable.
  TestHeaderMapImpl headers{{"x-forwarded-for", "10.0.0.1"},
                            {"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"},
                            {"x-envoy-force-trace", "true"}};
  EXPECT_CALL(random_, uuid()).Times(0);
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
      .WillOnce(Return(true));

  EXPECT_EQ((MutateRequestRet{"10.0.0.1:0", true}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ(UuidTraceStatus::Forced, UuidUtils::isTraceableUuid(headers.get_("x-request-id")));
}

// Forced, global off.
TEST_F(ConnectionManagerUtilityTest, NoTraceWhenForcedTracedButGlobalNotSet) {
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(false));
  // Internal request, make traceable.
  TestHeaderMapImpl headers{{"x-forwarded-for", "10.0.0.1"},
                            {"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"},
                            {"x-envoy-force-trace", "true"}};
  EXPECT_CALL(random_, uuid()).Times(0);
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
      .WillOnce(Return(false));

  EXPECT_EQ((MutateRequestRet{"10.0.0.1:0", true}),
            callMutateRequestHeaders(headers, Protocol::Http2));
  EXPECT_EQ(UuidTraceStatus::NoTrace, UuidUtils::isTraceableUuid(headers.get_("x-request-id")));
}

// Forced, global on, broken uuid.
TEST_F(ConnectionManagerUtilityTest, NoTraceOnBrokenUuid) {
  Http::TestHeaderMapImpl request_headers{{"x-envoy-force-trace", "true"}, {"x-request-id", "bb"}};
  callMutateRequestHeaders(request_headers, Protocol::Http2);

  EXPECT_EQ(UuidTraceStatus::NoTrace,
            UuidUtils::isTraceableUuid(request_headers.get_("x-request-id")));
}

TEST_F(ConnectionManagerUtilityTest, RemovesProxyResponseHeaders) {
  Http::TestHeaderMapImpl request_headers{{}};
  Http::TestHeaderMapImpl response_headers{{"keep-alive", "timeout=60"},
                                           {"proxy-connection", "proxy-header"}};
  ConnectionManagerUtility::mutateResponseHeaders(response_headers, &request_headers, "");

  EXPECT_EQ(UuidTraceStatus::NoTrace,
            UuidUtils::isTraceableUuid(request_headers.get_("x-request-id")));

  EXPECT_FALSE(response_headers.has("keep-alive"));
  EXPECT_FALSE(response_headers.has("proxy-connection"));
}

} // namespace Http
} // namespace Envoy
