#include "common/http/conn_manager_utility.h"
#include "common/http/headers.h"
#include "common/runtime/runtime_impl.h"
#include "common/runtime/uuid_util.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/utility.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::ReturnRefOfCopy;

namespace Http {

class ConnectionManagerUtilityTest : public testing::Test {
public:
  ConnectionManagerUtilityTest() {
    ON_CALL(config_, userAgent()).WillByDefault(ReturnRef(user_agent_));
    ON_CALL(config_, tracingConfig()).WillByDefault(ReturnRef(tracing_not_set_));

    set_tracing_all_.value({"test", Http::TracingType::All});
  }

  NiceMock<Network::MockConnection> connection_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<MockConnectionManagerConfig> config_;
  Optional<std::string> user_agent_;
  NiceMock<Runtime::MockLoader> runtime_;
  Optional<Http::TracingConnectionManagerConfig> tracing_not_set_;
  Optional<Http::TracingConnectionManagerConfig> set_tracing_all_;
};

TEST_F(ConnectionManagerUtilityTest, ShouldTraceRequest) {
  {
    NiceMock<Http::AccessLog::MockRequestInfo> request_info;
    EXPECT_FALSE(ConnectionManagerUtility::shouldTraceRequest(request_info, tracing_not_set_));
  }

  {
    NiceMock<Http::AccessLog::MockRequestInfo> request_info;
    EXPECT_TRUE(ConnectionManagerUtility::shouldTraceRequest(request_info, set_tracing_all_));
  }

  {
    NiceMock<Http::AccessLog::MockRequestInfo> request_info;
    Optional<Http::TracingConnectionManagerConfig> tracing_failure(
        {"operation", Http::TracingType::UpstreamFailure});
    EXPECT_CALL(request_info, failureReason())
        .WillOnce(Return(Http::AccessLog::FailureReason::UpstreamConnectionFailure));
    EXPECT_TRUE(ConnectionManagerUtility::shouldTraceRequest(request_info, tracing_failure));

    EXPECT_CALL(request_info, failureReason())
        .WillOnce(Return(Http::AccessLog::FailureReason::None));
    EXPECT_FALSE(ConnectionManagerUtility::shouldTraceRequest(request_info, tracing_failure));
  }
}

TEST_F(ConnectionManagerUtilityTest, UseRemoteAddressWhenNotLocalHostRemoteAddress) {
  const std::string not_local_host_remote_address = "12.12.12.12";
  EXPECT_CALL(config_, useRemoteAddress()).WillRepeatedly(Return(true));
  EXPECT_CALL(connection_, remoteAddress())
      .WillRepeatedly(ReturnRef(not_local_host_remote_address));

  TestHeaderMapImpl headers{};
  ConnectionManagerUtility::mutateRequestHeaders(headers, connection_, config_, random_, runtime_);

  EXPECT_TRUE(headers.has(Headers::get().ForwardedFor));
  EXPECT_EQ(not_local_host_remote_address, headers.get_(Headers::get().ForwardedFor));
}

TEST_F(ConnectionManagerUtilityTest, UseLocalAddressWhenLocalHostRemoteAddress) {
  const std::string local_host_remote_address = "127.0.0.1";
  const std::string local_address = "10.3.2.1";

  EXPECT_CALL(connection_, remoteAddress()).WillRepeatedly(ReturnRef(local_host_remote_address));
  EXPECT_CALL(config_, useRemoteAddress()).WillRepeatedly(Return(true));
  EXPECT_CALL(config_, localAddress()).WillRepeatedly(ReturnRef(local_address));

  TestHeaderMapImpl headers{};
  ConnectionManagerUtility::mutateRequestHeaders(headers, connection_, config_, random_, runtime_);

  EXPECT_TRUE(headers.has(Headers::get().ForwardedFor));
  EXPECT_EQ(local_address, headers.get_(Headers::get().ForwardedFor));
}

TEST_F(ConnectionManagerUtilityTest, UserAgentDontSet) {
  const std::string internal_remote_address = "10.0.0.1";

  EXPECT_CALL(config_, useRemoteAddress()).WillRepeatedly(Return(true));
  EXPECT_CALL(connection_, remoteAddress()).WillRepeatedly(ReturnRef(internal_remote_address));

  TestHeaderMapImpl headers{{"user-agent", "foo"}};
  ConnectionManagerUtility::mutateRequestHeaders(headers, connection_, config_, random_, runtime_);

  EXPECT_EQ("foo", headers.get_(Headers::get().UserAgent));
  EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceCluster));
  EXPECT_EQ("true", headers.get_(Headers::get().EnvoyInternalRequest));
}

TEST_F(ConnectionManagerUtilityTest, UserAgentSetWhenIncomingEmpty) {
  const std::string internal_remote_address = "10.0.0.1";

  EXPECT_CALL(config_, useRemoteAddress()).WillRepeatedly(Return(true));
  EXPECT_CALL(connection_, remoteAddress()).WillRepeatedly(ReturnRef(internal_remote_address));

  user_agent_.value("bar");
  TestHeaderMapImpl headers{{"user-agent", ""}, {"x-envoy-downstream-service-cluster", "foo"}};
  ConnectionManagerUtility::mutateRequestHeaders(headers, connection_, config_, random_, runtime_);

  EXPECT_EQ("bar", headers.get_(Headers::get().UserAgent));
  EXPECT_EQ("bar", headers.get_(Headers::get().EnvoyDownstreamServiceCluster));
  EXPECT_EQ("true", headers.get_(Headers::get().EnvoyInternalRequest));
}

TEST_F(ConnectionManagerUtilityTest, InternalServiceForceTrace) {
  const std::string external_remote_address = "34.0.0.1";
  const std::string internal_remote_address = "10.0.0.1";
  const std::string uuid = "f4dca0a9-12c7-4307-8002-969403baf480";

  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(false));
  ON_CALL(config_, tracingConfig()).WillByDefault(ReturnRef(set_tracing_all_));

  {
    // Internal request, make traceable
    TestHeaderMapImpl headers{{"x-forwarded-for", internal_remote_address},
                              {"x-request-id", uuid},
                              {"x-envoy-force-trace", "true"}};
    EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(true));
    ConnectionManagerUtility::mutateRequestHeaders(headers, connection_, config_, random_,
                                                   runtime_);

    EXPECT_EQ("f4dca0a9-12c7-a307-8002-969403baf480", headers.get_(Headers::get().RequestId));
  }

  {
    // Not internal request, force trace header should be cleaned.
    TestHeaderMapImpl headers{{"x-forwarded-for", external_remote_address},
                              {"x-request-id", uuid},
                              {"x-envoy-force-trace", "true"}};
    EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(true));
    ConnectionManagerUtility::mutateRequestHeaders(headers, connection_, config_, random_,
                                                   runtime_);
    EXPECT_EQ(uuid, headers.get_(Headers::get().RequestId));
    EXPECT_FALSE(headers.has(Headers::get().EnvoyForceTrace));
  }
}

TEST_F(ConnectionManagerUtilityTest, EdgeRequestRegenerateRequestIdAndWipeDownstream) {
  const std::string external_remote_address = "34.0.0.1";
  const std::string generated_uuid = "f4dca0a9-12c7-4307-8002-969403baf480";

  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));
  ON_CALL(connection_, remoteAddress()).WillByDefault(ReturnRef(external_remote_address));
  ON_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
      .WillByDefault(Return(true));
  ON_CALL(config_, tracingConfig()).WillByDefault(ReturnRef(set_tracing_all_));

  {
    TestHeaderMapImpl headers{{"x-envoy-downstream-service-cluster", "foo"},
                              {"x-request-id", "will_be_regenerated"}};
    EXPECT_CALL(random_, uuid()).WillOnce(Return(generated_uuid));

    EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.client_enabled", _)).Times(0);
    ConnectionManagerUtility::mutateRequestHeaders(headers, connection_, config_, random_,
                                                   runtime_);

    EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceCluster));
    // No changes to generated_uuid as x-client-trace-id is missing.
    EXPECT_EQ(generated_uuid, headers.get_(Headers::get().RequestId));
  }

  {
    // Runtime does not allow to make request traceable even though x-client-request-id set
    TestHeaderMapImpl headers{{"x-envoy-downstream-service-cluster", "foo"},
                              {"x-request-id", "will_be_regenerated"},
                              {"x-client-trace-id", "trace-id"}};
    EXPECT_CALL(random_, uuid()).WillOnce(Return(generated_uuid));
    EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.client_enabled", 100))
        .WillOnce(Return(false));

    ConnectionManagerUtility::mutateRequestHeaders(headers, connection_, config_, random_,
                                                   runtime_);

    EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceCluster));
    EXPECT_EQ("f4dca0a9-12c7-4307-8002-969403baf480", headers.get_(Headers::get().RequestId));
  }

  {
    // Runtime is enabled for tracing and x-client-request-id set

    TestHeaderMapImpl headers{{"x-envoy-downstream-service-cluster", "foo"},
                              {"x-request-id", "will_be_regenerated"},
                              {"x-client-trace-id", "trace-id"}};
    EXPECT_CALL(random_, uuid()).WillOnce(Return(generated_uuid));
    EXPECT_CALL(runtime_.snapshot_, featureEnabled("tracing.client_enabled", 100))
        .WillOnce(Return(true));

    ConnectionManagerUtility::mutateRequestHeaders(headers, connection_, config_, random_,
                                                   runtime_);

    EXPECT_FALSE(headers.has(Headers::get().EnvoyDownstreamServiceCluster));
    EXPECT_EQ("f4dca0a9-12c7-b307-8002-969403baf480", headers.get_(Headers::get().RequestId));
  }
}

TEST_F(ConnectionManagerUtilityTest, ExternalRequestPreserveRequestIdAndDownstream) {
  EXPECT_CALL(config_, useRemoteAddress()).WillRepeatedly(Return(false));
  EXPECT_CALL(connection_, remoteAddress()).Times(0);
  TestHeaderMapImpl headers{{"x-envoy-downstream-service-cluster", "foo"},
                            {"x-request-id", "id"},
                            {"x-forwarded-for", "34.0.0.1"}};

  ConnectionManagerUtility::mutateRequestHeaders(headers, connection_, config_, random_, runtime_);

  EXPECT_EQ("foo", headers.get_(Headers::get().EnvoyDownstreamServiceCluster));
  EXPECT_EQ("id", headers.get_(Headers::get().RequestId));
  EXPECT_FALSE(headers.has(Headers::get().EnvoyInternalRequest));
}

TEST_F(ConnectionManagerUtilityTest, UserAgentSetIncomingUserAgent) {
  const std::string internal_remote_address = "10.0.0.1";

  EXPECT_CALL(config_, useRemoteAddress()).WillRepeatedly(Return(true));
  EXPECT_CALL(connection_, remoteAddress()).WillRepeatedly(ReturnRef(internal_remote_address));

  user_agent_.value("bar");
  TestHeaderMapImpl headers{{"user-agent", "foo"}, {"x-envoy-downstream-service-cluster", "foo"}};
  ConnectionManagerUtility::mutateRequestHeaders(headers, connection_, config_, random_, runtime_);

  EXPECT_EQ("foo", headers.get_(Headers::get().UserAgent));
  EXPECT_EQ("bar", headers.get_(Headers::get().EnvoyDownstreamServiceCluster));
  EXPECT_EQ("true", headers.get_(Headers::get().EnvoyInternalRequest));
}

TEST_F(ConnectionManagerUtilityTest, UserAgentSetNoIncomingUserAgent) {
  const std::string internal_remote_address = "10.0.0.1";

  EXPECT_CALL(config_, useRemoteAddress()).WillRepeatedly(Return(true));
  EXPECT_CALL(connection_, remoteAddress()).WillRepeatedly(ReturnRef(internal_remote_address));

  user_agent_.value("bar");
  TestHeaderMapImpl headers{};
  ConnectionManagerUtility::mutateRequestHeaders(headers, connection_, config_, random_, runtime_);

  EXPECT_TRUE(headers.has(Headers::get().UserAgent));
  EXPECT_EQ("bar", headers.get_(Headers::get().UserAgent));
  EXPECT_EQ("bar", headers.get_(Headers::get().EnvoyDownstreamServiceCluster));
  EXPECT_EQ("true", headers.get_(Headers::get().EnvoyInternalRequest));
}

TEST_F(ConnectionManagerUtilityTest, RequestIdGeneratedWhenItsNotPresent) {
  {
    TestHeaderMapImpl headers{{":authority", "host"}, {":path", "/"}};
    EXPECT_CALL(random_, uuid()).WillOnce(Return("generated_uuid"));

    ConnectionManagerUtility::mutateRequestHeaders(headers, connection_, config_, random_,
                                                   runtime_);
    EXPECT_EQ("generated_uuid", headers.get_("x-request-id"));
  }

  {
    Runtime::RandomGeneratorImpl rand;
    TestHeaderMapImpl headers{{"x-client-trace-id", "trace-id"}};
    std::string uuid = rand.uuid();

    EXPECT_CALL(random_, uuid()).WillOnce(Return(uuid));

    ConnectionManagerUtility::mutateRequestHeaders(headers, connection_, config_, random_,
                                                   runtime_);
    // x-request-id should not be set to be traceable as it's not edge request
    EXPECT_EQ(uuid, headers.get_("x-request-id"));
  }
}

TEST_F(ConnectionManagerUtilityTest, DoNotOverrideRequestIdIfPresentWhenInternalRequest) {
  std::string local_remote_address = "10.0.0.1";
  EXPECT_CALL(config_, useRemoteAddress()).WillOnce(Return(true));
  EXPECT_CALL(connection_, remoteAddress()).WillRepeatedly(ReturnRef(local_remote_address));

  TestHeaderMapImpl headers{{"x-request-id", "original_request_id"}};
  EXPECT_CALL(random_, uuid()).Times(0);

  ConnectionManagerUtility::mutateRequestHeaders(headers, connection_, config_, random_, runtime_);
  EXPECT_EQ("original_request_id", headers.get_("x-request-id"));
}

TEST_F(ConnectionManagerUtilityTest, OverrideRequestIdForExternalRequests) {
  std::string external_ip = "134.2.2.11";
  EXPECT_CALL(connection_, remoteAddress()).WillRepeatedly(ReturnRef(external_ip));
  TestHeaderMapImpl headers{{"x-request-id", "original"}};

  EXPECT_CALL(random_, uuid()).WillOnce(Return("override"));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));

  ConnectionManagerUtility::mutateRequestHeaders(headers, connection_, config_, random_, runtime_);
  EXPECT_EQ("override", headers.get_("x-request-id"));
}

TEST_F(ConnectionManagerUtilityTest, ExternalAddressExternalRequestUseRemote) {
  ON_CALL(connection_, remoteAddress()).WillByDefault(ReturnRefOfCopy(std::string("50.0.0.1")));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));

  config_.route_config_.internal_only_headers_.push_back(LowerCaseString("custom_header"));

  TestHeaderMapImpl headers{{"x-envoy-downstream-service-cluster", "foo"},
                            {"x-envoy-retry-on", "foo"},
                            {"x-envoy-upstream-alt-stat-name", "foo"},
                            {"x-envoy-upstream-rq-timeout-ms", "foo"},
                            {"x-envoy-expected-rq-timeout-ms", "10"},
                            {"custom_header", "foo"}};
  ConnectionManagerUtility::mutateRequestHeaders(headers, connection_, config_, random_, runtime_);
  EXPECT_EQ("50.0.0.1", headers.get_("x-envoy-external-address"));
  EXPECT_FALSE(headers.has("x-envoy-internal"));
  EXPECT_FALSE(headers.has("x-envoy-downstream-service-cluster"));
  EXPECT_FALSE(headers.has("x-envoy-retry-on"));
  EXPECT_FALSE(headers.has("x-envoy-upstream-alt-stat-name"));
  EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
  EXPECT_FALSE(headers.has("x-envoy-expected-rq-timeout-ms"));
  EXPECT_FALSE(headers.has("custom_header"));
}

TEST_F(ConnectionManagerUtilityTest, ExternalAddressExternalRequestDontUseRemote) {
  ON_CALL(connection_, remoteAddress()).WillByDefault(ReturnRefOfCopy(std::string("50.0.0.1")));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(false));

  TestHeaderMapImpl headers{{"x-envoy-external-address", "60.0.0.1"},
                            {"x-forwarded-for", "60.0.0.1"}};
  ConnectionManagerUtility::mutateRequestHeaders(headers, connection_, config_, random_, runtime_);
  EXPECT_EQ("60.0.0.1", headers.get_("x-envoy-external-address"));
  EXPECT_EQ("60.0.0.1", headers.get_("x-forwarded-for"));
  EXPECT_FALSE(headers.has("x-envoy-internal"));
}

TEST_F(ConnectionManagerUtilityTest, ExternalAddressInternalRequestUseRemote) {
  ON_CALL(connection_, remoteAddress()).WillByDefault(ReturnRefOfCopy(std::string("10.0.0.1")));
  ON_CALL(config_, useRemoteAddress()).WillByDefault(Return(true));

  TestHeaderMapImpl headers{{"x-envoy-external-address", "60.0.0.1"},
                            {"x-envoy-expected-rq-timeout-ms", "10"}};
  ConnectionManagerUtility::mutateRequestHeaders(headers, connection_, config_, random_, runtime_);
  EXPECT_EQ("60.0.0.1", headers.get_("x-envoy-external-address"));
  EXPECT_EQ("10.0.0.1", headers.get_("x-forwarded-for"));
  EXPECT_EQ("10", headers.get_("x-envoy-expected-rq-timeout-ms"));
  EXPECT_TRUE(headers.has("x-envoy-internal"));
}

TEST_F(ConnectionManagerUtilityTest, MutateResponseHeaders) {
  config_.route_config_.response_headers_to_remove_.push_back(LowerCaseString("custom_header"));
  config_.route_config_.response_headers_to_add_.push_back({LowerCaseString("to_add"), "foo"});

  TestHeaderMapImpl response_headers{
      {"connection", "foo"}, {"transfer-encoding", "foo"}, {"custom_header", "foo"}};
  TestHeaderMapImpl request_headers{{"x-request-id", "request-id"}};

  ConnectionManagerUtility::mutateResponseHeaders(response_headers, request_headers, config_);

  EXPECT_EQ(1UL, response_headers.size());
  EXPECT_EQ("foo", response_headers.get_("to_add"));
  EXPECT_FALSE(response_headers.has("x-request-id"));
}

TEST_F(ConnectionManagerUtilityTest, MutateResponseHeadersReturnXRequestId) {
  TestHeaderMapImpl response_headers;
  TestHeaderMapImpl request_headers{{"x-request-id", "request-id"},
                                    {"x-envoy-force-trace", "true"}};

  ConnectionManagerUtility::mutateResponseHeaders(response_headers, request_headers, config_);
  EXPECT_EQ("request-id", response_headers.get_("x-request-id"));
}

} // Http
