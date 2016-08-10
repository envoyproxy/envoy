#include "common/http/headers.h"
#include "common/http/header_map_impl.h"
#include "common/runtime/runtime_impl.h"
#include "common/runtime/uuid_util.h"
#include "common/tracing/http_tracer_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/mocks.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::Test;

namespace Tracing {

TEST(HttpTracerUtilityTest, IsTracing) {
  NiceMock<Http::AccessLog::MockRequestInfo> request_info;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Stats::MockStore> stats;
  Runtime::RandomGeneratorImpl random;
  std::string not_traceable_guid = random.uuid();
  std::string traceable_guid = random.uuid();
  UuidUtils::setTraceableUuid(traceable_guid);

  Http::HeaderMapImpl traceable_header{{"x-request-id", traceable_guid}};
  Http::HeaderMapImpl not_traceable_header{{"x-request-id", not_traceable_guid}};
  Http::HeaderMapImpl empty_header{};

  // Global tracing enabled and x-request-id is traceable.
  {
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(true));
    EXPECT_CALL(request_info, healthCheck()).WillOnce(Return(false));

    Decision result = HttpTracerUtility::isTracing(request_info, traceable_header, runtime);
    EXPECT_EQ(Reason::TraceableRequest, result.reason);
    EXPECT_TRUE(result.is_tracing);
  }

  // Global tracing disabled and x-request-id is traceable.
  {
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(false));
    EXPECT_CALL(request_info, healthCheck()).WillOnce(Return(false));

    Decision result = HttpTracerUtility::isTracing(request_info, traceable_header, runtime);
    EXPECT_EQ(Reason::GlobalSwitchOff, result.reason);
    EXPECT_FALSE(result.is_tracing);
  }

  // x-request-id is not traceable, sampling is on, global is on.
  {
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(true));
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.random_sampling", 0, _, 10000))
        .WillOnce(Return(true));
    EXPECT_CALL(request_info, healthCheck()).WillOnce(Return(false));

    Decision result = HttpTracerUtility::isTracing(request_info, not_traceable_header, runtime);
    EXPECT_EQ(Reason::Sampling, result.reason);
    EXPECT_TRUE(result.is_tracing);
  }

  // HC request.
  {
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _)).Times(0);
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.random_sampling", 0, _, 10000)).Times(0);

    Http::HeaderMapImpl traceable_header_hc{{"x-request-id", traceable_guid}};
    EXPECT_CALL(request_info, healthCheck()).WillOnce(Return(true));

    Decision result = HttpTracerUtility::isTracing(request_info, traceable_header_hc, runtime);
    EXPECT_EQ(Reason::HealthCheck, result.reason);
    EXPECT_FALSE(result.is_tracing);
  }

  // x-request-id is not traceable, sampling is on, global is off.
  {
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(false));
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.random_sampling", 0, _, 10000))
        .WillOnce(Return(true));
    EXPECT_CALL(request_info, healthCheck()).WillOnce(Return(false));

    Decision result = HttpTracerUtility::isTracing(request_info, not_traceable_header, runtime);
    EXPECT_EQ(Reason::GlobalSwitchOff, result.reason);
    EXPECT_FALSE(result.is_tracing);
  }

  // x-request-id is traceable, client called.
  {
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(true));
    Http::HeaderMapImpl traceable_header_client{{"x-request-id", traceable_guid},
                                                {"x-client-trace-id", random.uuid()}};
    EXPECT_CALL(request_info, healthCheck()).WillOnce(Return(false));

    Decision result = HttpTracerUtility::isTracing(request_info, traceable_header_client, runtime);
    EXPECT_EQ(Reason::ClientForced, result.reason);
    EXPECT_TRUE(result.is_tracing);
  }

  // x-request-id is traceable, service forced.
  {
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(true));
    Http::HeaderMapImpl traceable_header_client{{"x-request-id", traceable_guid},
                                                {"x-envoy-force-trace", random.uuid()}};
    EXPECT_CALL(request_info, healthCheck()).WillOnce(Return(false));

    Decision result = HttpTracerUtility::isTracing(request_info, traceable_header_client, runtime);
    EXPECT_EQ(Reason::ServiceForced, result.reason);
    EXPECT_TRUE(result.is_tracing);
  }
}

TEST(HttpTracerImplTest, AllSinksTraceableRequest) {
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Stats::MockStore> stats;
  Runtime::RandomGeneratorImpl random;
  std::string not_traceable_guid = random.uuid();
  std::string traceable_guid = random.uuid();
  UuidUtils::setTraceableUuid(traceable_guid);

  Http::HeaderMapImpl traceable_header{{"x-request-id", traceable_guid}};
  Http::HeaderMapImpl not_traceable_header{{"x-request-id", not_traceable_guid}};
  Http::HeaderMapImpl empty_header{};
  NiceMock<Http::AccessLog::MockRequestInfo> request_info;

  MockHttpSink* sink1 = new MockHttpSink();
  MockHttpSink* sink2 = new MockHttpSink();

  HttpTracerImpl tracer(runtime, stats);
  tracer.addSink(HttpSinkPtr{sink1});
  tracer.addSink(HttpSinkPtr{sink2});

  // Global tracing enabled and x-request-id is traceable.
  {
    EXPECT_CALL(*sink1, flushTrace(_, _, _));
    EXPECT_CALL(*sink2, flushTrace(_, _, _));
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(true));
    tracer.trace(&traceable_header, &empty_header, request_info);
  }

  // Global tracing disabled and x-request-id is traceable.
  {
    EXPECT_CALL(*sink1, flushTrace(_, _, _)).Times(0);
    EXPECT_CALL(*sink2, flushTrace(_, _, _)).Times(0);
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(false));

    tracer.trace(&traceable_header, &empty_header, request_info);
  }

  // x-request-id is not traceable, sampling is on, global is on.
  {
    EXPECT_CALL(*sink1, flushTrace(_, _, _));
    EXPECT_CALL(*sink2, flushTrace(_, _, _));
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(true));
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.random_sampling", 0, _, 10000))
        .WillOnce(Return(true));

    tracer.trace(&not_traceable_header, &empty_header, request_info);
  }

  // HC request.
  {
    EXPECT_CALL(*sink1, flushTrace(_, _, _)).Times(0);
    EXPECT_CALL(*sink2, flushTrace(_, _, _)).Times(0);
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _)).Times(0);
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.random_sampling", 0, _, 10000)).Times(0);

    Http::HeaderMapImpl traceable_header_hc{{"x-request-id", traceable_guid}};
    NiceMock<Http::AccessLog::MockRequestInfo> request_info;
    EXPECT_CALL(request_info, healthCheck()).WillOnce(Return(true));
    tracer.trace(&traceable_header_hc, &empty_header, request_info);
  }

  // x-request-id is not traceable, sampling is on, global is off.
  {
    EXPECT_CALL(*sink1, flushTrace(_, _, _)).Times(0);
    EXPECT_CALL(*sink2, flushTrace(_, _, _)).Times(0);
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(false));
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.random_sampling", 0, _, 10000))
        .WillOnce(Return(true));

    tracer.trace(&not_traceable_header, &empty_header, request_info);
  }

  // x-request-id is not traceable, sampling is off, global not called.
  {
    EXPECT_CALL(*sink1, flushTrace(_, _, _)).Times(0);
    EXPECT_CALL(*sink2, flushTrace(_, _, _)).Times(0);
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _)).Times(0);
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.random_sampling", 0, _, 10000))
        .WillOnce(Return(false));

    tracer.trace(&not_traceable_header, &empty_header, request_info);
  }
}

TEST(HttpTracerImplTest, ZeroSinksRunsFine) {
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Stats::MockStore> stats;
  HttpTracerImpl tracer(runtime, stats);
  Runtime::RandomGeneratorImpl random;
  std::string not_traceable_guid = random.uuid();

  Http::HeaderMapImpl not_traceable{{"x-request-id", not_traceable_guid}};

  EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
      .WillOnce(Return(true));
  EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.random_sampling", 0, _, 10000))
      .WillOnce(Return(true));

  NiceMock<Http::AccessLog::MockRequestInfo> request_info;
  tracer.trace(&not_traceable, &not_traceable, request_info);
}

TEST(HttpNullTracerTest, NoFailures) {
  HttpNullTracer tracer;
  NiceMock<Stats::MockStore> store;
  HttpSink* sink = new NiceMock<Tracing::MockHttpSink>();

  tracer.addSink(HttpSinkPtr{sink});

  Http::HeaderMapImpl empty_header{};
  Http::AccessLog::MockRequestInfo request_info;

  tracer.trace(&empty_header, &empty_header, request_info);
}

class LightStepSinkTest : public Test {
public:
  LightStepSinkTest()
      : stats_{LIGHTSTEP_STATS(POOL_COUNTER_PREFIX(fake_stats_, "prefix.tracing.lightstep."))} {}

  void setup(Json::Object& config) {
    sink_.reset(new LightStepSink(config, cm_, tls_, "prefix.", fake_stats_, random_,
                                  "service_cluster", "service_node", "token"));
  }

  void setupValidSink() {
    EXPECT_CALL(cm_, has("lightstep_saas")).WillOnce(Return(true));

    std::string valid_config = R"EOF(
      {"collector_cluster": "lightstep_saas"}
    )EOF";
    Json::StringLoader loader(valid_config);

    setup(loader);
  }

  const Http::HeaderMapImpl empty_header_{};

  Stats::IsolatedStoreImpl fake_stats_;
  LightStepStats stats_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  std::unique_ptr<LightStepSink> sink_;
};

TEST_F(LightStepSinkTest, InitializeSink) {
  {
    std::string invalid_config = R"EOF(
      {"fake" : "fake"}
    )EOF";
    Json::StringLoader loader(invalid_config);

    EXPECT_THROW(setup(loader), EnvoyException);
  }

  {
    std::string empty_config = "{}";
    Json::StringLoader loader(empty_config);

    EXPECT_THROW(setup(loader), EnvoyException);
  }

  {
    // Valid config but not valid cluster
    EXPECT_CALL(cm_, has("lightstep_saas")).WillOnce(Return(false));

    std::string valid_config = R"EOF(
      {"collector_cluster": "lightstep_saas"}
    )EOF";
    Json::StringLoader loader(valid_config);

    EXPECT_THROW(setup(loader), EnvoyException);
  }

  {
    EXPECT_CALL(cm_, has("lightstep_saas")).WillOnce(Return(true));

    std::string valid_config = R"EOF(
      {"collector_cluster": "lightstep_saas"}
    )EOF";
    Json::StringLoader loader(valid_config);

    setup(loader);
  }
}

TEST_F(LightStepSinkTest, CallbacksCalled) {
  setupValidSink();

  NiceMock<Http::AccessLog::MockRequestInfo> request_info;

  NiceMock<Http::MockAsyncClient>* client_1 = new NiceMock<Http::MockAsyncClient>();
  EXPECT_CALL(cm_, httpAsyncClientForCluster_("lightstep_saas")).WillOnce(Return(client_1));

  Http::MockAsyncClientRequest* request_1 = new Http::MockAsyncClientRequest(client_1);
  Http::AsyncClient::Callbacks* callback_1;
  const Optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));

  EXPECT_CALL(*client_1, send_(_, _, timeout))
      .WillOnce(
          Invoke([&](Http::MessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            callback_1 = &callbacks;
            return request_1;
          }));
  EXPECT_CALL(random_, uuid()).WillOnce(Return("1")).WillOnce(Return("2"));
  SystemTime start_time_1;
  EXPECT_CALL(request_info, startTime()).WillOnce(Return(start_time_1));
  std::chrono::seconds duration_1(1);
  EXPECT_CALL(request_info, duration()).WillOnce(Return(duration_1));
  Optional<uint32_t> code_1(200);
  EXPECT_CALL(request_info, responseCode()).WillRepeatedly(ReturnRef(code_1));
  const std::string protocol = "http/1";
  EXPECT_CALL(request_info, protocol()).WillRepeatedly(ReturnRef(protocol));

  sink_->flushTrace(empty_header_, empty_header_, request_info);

  NiceMock<Http::MockAsyncClient>* client_2 = new NiceMock<Http::MockAsyncClient>();
  Http::MockAsyncClientRequest* request_2 = new Http::MockAsyncClientRequest(client_2);
  EXPECT_CALL(cm_, httpAsyncClientForCluster_("lightstep_saas")).WillOnce(Return(client_2));
  Http::AsyncClient::Callbacks* callback_2;

  EXPECT_CALL(*client_2, send_(_, _, _))
      .WillOnce(Invoke([&](Http::MessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                           Optional<std::chrono::milliseconds>) -> Http::AsyncClient::Request* {
        callback_2 = &callbacks;
        return request_2;
      }));
  EXPECT_CALL(random_, uuid()).WillOnce(Return("3")).WillOnce(Return("4"));
  SystemTime start_time_2;
  EXPECT_CALL(request_info, startTime()).WillOnce(Return(start_time_2));
  std::chrono::seconds duration_2(2);
  EXPECT_CALL(request_info, duration()).WillOnce(Return(duration_2));
  Optional<uint32_t> code_2(200);
  EXPECT_CALL(request_info, responseCode()).WillRepeatedly(ReturnRef(code_2));

  sink_->flushTrace(empty_header_, empty_header_, request_info);

  callback_2->onFailure(Http::AsyncClient::FailureReason::Reset);
  EXPECT_EQ(1UL, stats_.collector_failed_.value());
  EXPECT_EQ(0UL, stats_.collector_success_.value());

  Http::MessagePtr msg{new Http::RequestMessageImpl()};
  callback_1->onSuccess(std::move(msg));
  EXPECT_EQ(1UL, stats_.collector_failed_.value());
  EXPECT_EQ(1UL, stats_.collector_success_.value());

  // Shutdown sink and try to make trace
  tls_.shutdownThread_();

  EXPECT_CALL(cm_, httpAsyncClientForCluster_("lightstep_saas")).Times(0);
  sink_->flushTrace(empty_header_, empty_header_, request_info);
}

TEST_F(LightStepSinkTest, ClientNotAvailable) {
  setupValidSink();

  NiceMock<Http::AccessLog::MockRequestInfo> request_info;

  EXPECT_CALL(cm_, httpAsyncClientForCluster_("lightstep_saas")).WillOnce(Return(nullptr));
  sink_->flushTrace(empty_header_, empty_header_, request_info);

  EXPECT_EQ(1UL, stats_.client_failed_.value());
  EXPECT_EQ(0UL, stats_.collector_failed_.value());
  EXPECT_EQ(0UL, stats_.collector_success_.value());
}

TEST_F(LightStepSinkTest, ShutdownWhenActiveRequests) {
  setupValidSink();

  NiceMock<Http::MockAsyncClient>* client = new NiceMock<Http::MockAsyncClient>();
  EXPECT_CALL(cm_, httpAsyncClientForCluster_("lightstep_saas")).WillOnce(Return(client));

  Http::MockAsyncClientRequest* request = new Http::MockAsyncClientRequest(client);

  NiceMock<Http::AccessLog::MockRequestInfo> request_info;
  const std::string protocol = "http/1";
  EXPECT_CALL(request_info, protocol()).WillOnce(ReturnRef(protocol));
  EXPECT_CALL(random_, uuid()).WillOnce(Return("1")).WillOnce(Return("2"));
  SystemTime start_time(std::chrono::duration<int>(1));
  EXPECT_CALL(request_info, startTime()).WillOnce(Return(start_time));
  std::chrono::seconds duration(1);
  EXPECT_CALL(request_info, duration()).WillOnce(Return(duration));
  Optional<uint32_t> code(200);
  EXPECT_CALL(request_info, responseCode()).WillRepeatedly(ReturnRef(code));
  Http::HeaderMapImpl request_header{{"x-request-id", "id"},
                                     {":method", "GET"},
                                     {":path", "sample_path"},
                                     {"x-envoy-downstream-service-cluster", "downstream"},
                                     {"x-client-trace-id", "client-trace-id"},
                                     {"user-agent", "agent"}};

  std::string expected_json = R"EOF(
{
  "runtime": {
    "guid": "1",
    "group_name": "Envoy-Tracing",
    "start_micros": 1000000
  },
  "span_records": [
    {
      "span_guid": "2",
      "span_name": "service_cluster",
      "oldest_micros": 1000000,
      "youngest_micros": 2000000,
      "join_ids": [
      {
        "TraceKey": "x-request-id",
        "Value": "id"
      }
      ,{
        "TraceKey": "x-client-trace-id",
        "Value": "client-trace-id"
      }],
      "attributes": [
      {
        "Key": "request line",
        "Value": "GET sample_path http/1"
      },
      {
        "Key": "response code",
        "Value": "200"
      },
      {
        "Key": "downstream cluster",
        "Value": "downstream"
      },
      {
        "Key": "user agent",
        "Value": "agent"
      },
      {
        "Key": "node id",
        "Value": "service_node"
      }]
    }
  ]
}
  )EOF";

  Http::AsyncClient::Callbacks* callback;
  EXPECT_CALL(*client, send_(_, _, _))
      .WillOnce(Invoke([&](Http::MessagePtr& msg, Http::AsyncClient::Callbacks& callbacks,
                           Optional<std::chrono::milliseconds>) -> Http::AsyncClient::Request* {
        callback = &callbacks;
        EXPECT_EQ(expected_json, msg->bodyAsString());
        EXPECT_EQ("token", msg->headers().get("LightStep-Access-Token"));
        return request;
      }));

  sink_->flushTrace(request_header, empty_header_, request_info);

  EXPECT_CALL(*request, cancel());
  tls_.shutdownThread_();
}

TEST(LightStepUtilityTest, HeadersNotSet) {
  std::string expected_json = R"EOF(
{
  "runtime": {
    "guid": "1",
    "group_name": "Envoy-Tracing",
    "start_micros": 1000000
  },
  "span_records": [
    {
      "span_guid": "2",
      "span_name": "cluster",
      "oldest_micros": 1000000,
      "youngest_micros": 2000000,
      "join_ids": [
      {
        "TraceKey": "x-request-id",
        "Value": "id"
      }],
      "attributes": [
      {
        "Key": "request line",
        "Value": "POST /locations http/1"
      },
      {
        "Key": "response code",
        "Value": "300"
      },
      {
        "Key": "downstream cluster",
        "Value": "-"
      },
      {
        "Key": "user agent",
        "Value": "-"
      },
      {
        "Key": "node id",
        "Value": "i485"
      }]
    }
  ]
}
  )EOF";

  NiceMock<Http::AccessLog::MockRequestInfo> request_info;
  const std::string protocol = "http/1";
  EXPECT_CALL(request_info, protocol()).WillOnce(ReturnRef(protocol));
  SystemTime start_time(std::chrono::duration<int>(1));
  EXPECT_CALL(request_info, startTime()).WillOnce(Return(start_time));
  std::chrono::seconds duration(1);
  EXPECT_CALL(request_info, duration()).WillOnce(Return(duration));
  Optional<uint32_t> code(300);
  EXPECT_CALL(request_info, responseCode()).WillRepeatedly(ReturnRef(code));

  Runtime::MockRandomGenerator random;
  EXPECT_CALL(random, uuid()).WillOnce(Return("1")).WillOnce(Return("2"));

  Http::HeaderMapImpl request_header{
      {"x-request-id", "id"}, {":method", "POST"}, {":path", "/locations"}};
  Http::HeaderMapImpl empty_header;

  const std::string actual_json = LightStepUtility::buildJsonBody(
      request_header, empty_header, request_info, random, "cluster", "i485");

  EXPECT_EQ(actual_json, expected_json);
}

} // Tracing
