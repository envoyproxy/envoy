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

TEST(HttpTracerUtilityTest, mutateHeaders) {
  // Sampling, global on.
  {
    NiceMock<Runtime::MockLoader> runtime;
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.random_sampling", 0, _, 10000))
        .WillOnce(Return(true));
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(true));

    Http::HeaderMapImpl request_headers{{"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"}};
    HttpTracerUtility::mutateHeaders(request_headers, runtime);

    EXPECT_EQ(UuidTraceStatus::Sampled,
              UuidUtils::isTraceableUuid(request_headers.get("x-request-id")));
  }

  // Sampling, global off.
  {
    NiceMock<Runtime::MockLoader> runtime;
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.random_sampling", 0, _, 10000))
        .WillOnce(Return(true));
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(false));

    Http::HeaderMapImpl request_headers{{"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"}};
    HttpTracerUtility::mutateHeaders(request_headers, runtime);

    EXPECT_EQ(UuidTraceStatus::NoTrace,
              UuidUtils::isTraceableUuid(request_headers.get("x-request-id")));
  }

  // Client, client enabled, global on.
  {
    NiceMock<Runtime::MockLoader> runtime;
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.client_enabled", 100))
        .WillOnce(Return(true));
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(true));

    Http::HeaderMapImpl request_headers{
        {"x-client-trace-id", "f4dca0a9-12c7-4307-8002-969403baf480"},
        {"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"}};
    HttpTracerUtility::mutateHeaders(request_headers, runtime);

    EXPECT_EQ(UuidTraceStatus::Client,
              UuidUtils::isTraceableUuid(request_headers.get("x-request-id")));
  }

  // Client, client disabled, global on.
  {
    NiceMock<Runtime::MockLoader> runtime;
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.client_enabled", 100))
        .WillOnce(Return(false));
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(true));

    Http::HeaderMapImpl request_headers{
        {"x-client-trace-id", "f4dca0a9-12c7-4307-8002-969403baf480"},
        {"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"}};
    HttpTracerUtility::mutateHeaders(request_headers, runtime);

    EXPECT_EQ(UuidTraceStatus::NoTrace,
              UuidUtils::isTraceableUuid(request_headers.get("x-request-id")));
  }

  // Forced, global on.
  {
    NiceMock<Runtime::MockLoader> runtime;
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(true));

    Http::HeaderMapImpl request_headers{{"x-envoy-force-trace", "true"},
                                        {"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"}};
    HttpTracerUtility::mutateHeaders(request_headers, runtime);

    EXPECT_EQ(UuidTraceStatus::Forced,
              UuidUtils::isTraceableUuid(request_headers.get("x-request-id")));
  }

  // Forced, global off.
  {
    NiceMock<Runtime::MockLoader> runtime;
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(false));

    Http::HeaderMapImpl request_headers{{"x-envoy-force-trace", "true"},
                                        {"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"}};
    HttpTracerUtility::mutateHeaders(request_headers, runtime);

    EXPECT_EQ(UuidTraceStatus::NoTrace,
              UuidUtils::isTraceableUuid(request_headers.get("x-request-id")));
  }

  // Forced, global on, broken uuid.
  {
    NiceMock<Runtime::MockLoader> runtime;

    Http::HeaderMapImpl request_headers{{"x-envoy-force-trace", "true"}, {"x-request-id", "bb"}};
    HttpTracerUtility::mutateHeaders(request_headers, runtime);

    EXPECT_EQ(UuidTraceStatus::NoTrace,
              UuidUtils::isTraceableUuid(request_headers.get("x-request-id")));
  }
}

TEST(HttpTracerUtilityTest, IsTracing) {
  NiceMock<Http::AccessLog::MockRequestInfo> request_info;
  NiceMock<Stats::MockStore> stats;
  Runtime::RandomGeneratorImpl random;
  std::string not_traceable_guid = random.uuid();

  std::string forced_guid = random.uuid();
  UuidUtils::setTraceableUuid(forced_guid, UuidTraceStatus::Forced);
  Http::HeaderMapImpl forced_header{{"x-request-id", forced_guid}};

  std::string sampled_guid = random.uuid();
  UuidUtils::setTraceableUuid(sampled_guid, UuidTraceStatus::Sampled);
  Http::HeaderMapImpl sampled_header{{"x-request-id", sampled_guid}};

  std::string client_guid = random.uuid();
  UuidUtils::setTraceableUuid(client_guid, UuidTraceStatus::Client);
  Http::HeaderMapImpl client_header{{"x-request-id", client_guid}};

  Http::HeaderMapImpl not_traceable_header{{"x-request-id", not_traceable_guid}};
  Http::HeaderMapImpl empty_header{};

  // Force traced.
  {
    EXPECT_CALL(request_info, healthCheck()).WillOnce(Return(false));

    Decision result = HttpTracerUtility::isTracing(request_info, forced_header);
    EXPECT_EQ(Reason::ServiceForced, result.reason);
    EXPECT_TRUE(result.is_tracing);
  }

  // Sample traced.
  {
    EXPECT_CALL(request_info, healthCheck()).WillOnce(Return(false));

    Decision result = HttpTracerUtility::isTracing(request_info, sampled_header);
    EXPECT_EQ(Reason::Sampling, result.reason);
    EXPECT_TRUE(result.is_tracing);
  }

  // HC request.
  {
    Http::HeaderMapImpl traceable_header_hc{{"x-request-id", forced_guid}};
    EXPECT_CALL(request_info, healthCheck()).WillOnce(Return(true));

    Decision result = HttpTracerUtility::isTracing(request_info, traceable_header_hc);
    EXPECT_EQ(Reason::HealthCheck, result.reason);
    EXPECT_FALSE(result.is_tracing);
  }

  // Client traced.
  {
    EXPECT_CALL(request_info, healthCheck()).WillOnce(Return(false));

    Decision result = HttpTracerUtility::isTracing(request_info, client_header);
    EXPECT_EQ(Reason::ClientForced, result.reason);
    EXPECT_TRUE(result.is_tracing);
  }
}

TEST(HttpTracerImplTest, AllSinksTraceableRequest) {
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Stats::MockStore> stats;
  Runtime::RandomGeneratorImpl random;
  std::string not_traceable_guid = random.uuid();

  std::string forced_guid = random.uuid();
  UuidUtils::setTraceableUuid(forced_guid, UuidTraceStatus::Forced);
  Http::HeaderMapImpl forced_header{{"x-request-id", forced_guid}};

  std::string sampled_guid = random.uuid();
  UuidUtils::setTraceableUuid(sampled_guid, UuidTraceStatus::Sampled);
  Http::HeaderMapImpl sampled_header{{"x-request-id", sampled_guid}};

  Http::HeaderMapImpl not_traceable_header{{"x-request-id", not_traceable_guid}};
  Http::HeaderMapImpl empty_header{};
  NiceMock<Http::AccessLog::MockRequestInfo> request_info;

  MockHttpSink* sink1 = new MockHttpSink();
  MockHttpSink* sink2 = new MockHttpSink();

  HttpTracerImpl tracer(runtime, stats);
  tracer.addSink(HttpSinkPtr{sink1});
  tracer.addSink(HttpSinkPtr{sink2});

  // Force traced request.
  {
    EXPECT_CALL(*sink1, flushTrace(_, _, _));
    EXPECT_CALL(*sink2, flushTrace(_, _, _));
    tracer.trace(&forced_header, &empty_header, request_info);
  }

  // x-request-id is sample traced.
  {
    EXPECT_CALL(*sink1, flushTrace(_, _, _));
    EXPECT_CALL(*sink2, flushTrace(_, _, _));

    tracer.trace(&sampled_header, &empty_header, request_info);
  }

  // HC request.
  {
    EXPECT_CALL(*sink1, flushTrace(_, _, _)).Times(0);
    EXPECT_CALL(*sink2, flushTrace(_, _, _)).Times(0);

    Http::HeaderMapImpl traceable_header_hc{{"x-request-id", forced_guid}};
    NiceMock<Http::AccessLog::MockRequestInfo> request_info;
    EXPECT_CALL(request_info, healthCheck()).WillOnce(Return(true));
    tracer.trace(&traceable_header_hc, &empty_header, request_info);
  }

  // x-request-id is not traceable.
  {
    EXPECT_CALL(*sink1, flushTrace(_, _, _)).Times(0);
    EXPECT_CALL(*sink2, flushTrace(_, _, _)).Times(0);

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
    sink_.reset(
        new LightStepSink(config, cm_, "prefix.", fake_stats_, "service_node", lightstep_options_));
  }

  void setupValidSink() {
    EXPECT_CALL(cm_, get("lightstep_saas"));

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
  NiceMock<Runtime::MockRandomGenerator> random_;
  std::unique_ptr<LightStepSink> sink_;
  lightstep::TracerOptions lightstep_options_;
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
    EXPECT_CALL(cm_, get("lightstep_saas")).WillOnce(Return(nullptr));

    std::string valid_config = R"EOF(
      {"collector_cluster": "lightstep_saas"}
    )EOF";
    Json::StringLoader loader(valid_config);

    EXPECT_THROW(setup(loader), EnvoyException);
  }

  {
    EXPECT_CALL(cm_, get("lightstep_saas"));

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

  EXPECT_CALL(cm_, httpAsyncClientForCluster("lightstep_saas"))
      .WillOnce(ReturnRef(cm_.async_client_));

  Http::MockAsyncClientRequest request_1(&cm_.async_client_);
  Http::AsyncClient::Callbacks* callback_1;
  const Optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));

  EXPECT_CALL(cm_.async_client_, send_(_, _, timeout))
      .WillOnce(
          Invoke([&](Http::MessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            callback_1 = &callbacks;
            return &request_1;
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

  Http::MockAsyncClientRequest request_2(&cm_.async_client_);
  EXPECT_CALL(cm_, httpAsyncClientForCluster("lightstep_saas"))
      .WillOnce(ReturnRef(cm_.async_client_));
  Http::AsyncClient::Callbacks* callback_2;

  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillOnce(Invoke([&](Http::MessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                           Optional<std::chrono::milliseconds>) -> Http::AsyncClient::Request* {
        callback_2 = &callbacks;
        return &request_2;
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

  callback_1->onSuccess(Http::MessagePtr{new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::HeaderMapImpl{{":status", "200"}}})});
  EXPECT_EQ(1UL, stats_.collector_failed_.value());
  EXPECT_EQ(1UL, stats_.collector_success_.value());
}

TEST_F(LightStepSinkTest, ClientNotAvailable) {
  setupValidSink();

  NiceMock<Http::AccessLog::MockRequestInfo> request_info;

  EXPECT_CALL(cm_, httpAsyncClientForCluster("lightstep_saas"))
      .WillOnce(ReturnRef(cm_.async_client_));
  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::MessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            callbacks.onSuccess(Http::MessagePtr{new Http::ResponseMessageImpl(
                Http::HeaderMapPtr{new Http::HeaderMapImpl{{":status", "503"}}})});
            return nullptr;
          }));
  SystemTime start_time_1;
  EXPECT_CALL(request_info, startTime()).WillOnce(Return(start_time_1));
  std::chrono::seconds duration_1(1);
  EXPECT_CALL(request_info, duration()).WillOnce(Return(duration_1));
  const std::string protocol = "http/1";
  EXPECT_CALL(request_info, protocol()).WillRepeatedly(ReturnRef(protocol));
  Optional<uint32_t> code_1(200);
  EXPECT_CALL(request_info, responseCode()).WillRepeatedly(ReturnRef(code_1));
  sink_->flushTrace(empty_header_, empty_header_, request_info);

  EXPECT_EQ(1UL, stats_.collector_failed_.value());
  EXPECT_EQ(0UL, stats_.collector_success_.value());
}

TEST_F(LightStepSinkTest, ShutdownWhenActiveRequests) {
  setupValidSink();

  EXPECT_CALL(cm_, httpAsyncClientForCluster("lightstep_saas"))
      .WillOnce(ReturnRef(cm_.async_client_));

  Http::MockAsyncClientRequest request(&cm_.async_client_);

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

  Http::AsyncClient::Callbacks* callback;
  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillOnce(Invoke([&](Http::MessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                           Optional<std::chrono::milliseconds>) -> Http::AsyncClient::Request* {
        callback = &callbacks;
        return &request;
      }));

  sink_->flushTrace(request_header, empty_header_, request_info);
}

} // Tracing
