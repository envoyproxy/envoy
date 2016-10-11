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

  // Sampling must not be done on client traced.
  {
    NiceMock<Runtime::MockLoader> runtime;
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.random_sampling", 0, _, 10000)).Times(0);
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(true));

    Http::HeaderMapImpl request_headers{{"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}};
    HttpTracerUtility::mutateHeaders(request_headers, runtime);

    EXPECT_EQ(UuidTraceStatus::Forced,
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
  void setup(Json::Object& config, bool init_timer) {
    std::unique_ptr<lightstep::TracerOptions> opts(new lightstep::TracerOptions());
    opts->access_token = "sample_token";
    opts->tracer_attributes["lightstep.component_name"] = "component";

    ON_CALL(cm_, httpAsyncClientForCluster("lightstep_saas"))
        .WillByDefault(ReturnRef(cm_.async_client_));

    if (init_timer) {
      timer_ = new NiceMock<Event::MockTimer>(&tls_.dispatcher_);
      EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(1000)));
    }

    sink_.reset(
        new LightStepSink(config, cm_, stats_, "service_node", tls_, runtime_, std::move(opts)));
  }

  void setupValidSink() {
    EXPECT_CALL(cm_, get("lightstep_saas")).WillRepeatedly(Return(&cluster_));
    ON_CALL(cluster_, features()).WillByDefault(Return(Upstream::Cluster::Features::HTTP2));

    std::string valid_config = R"EOF(
      {"collector_cluster": "lightstep_saas"}
    )EOF";
    Json::StringLoader loader(valid_config);

    setup(loader, true);
  }

  const Http::HeaderMapImpl empty_header_{};
  const Http::HeaderMapImpl response_headers_{{":status", "500"}};

  std::unique_ptr<LightStepSink> sink_;
  NiceMock<Event::MockTimer>* timer_;
  Stats::IsolatedStoreImpl stats_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Upstream::MockCluster> cluster_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<ThreadLocal::MockInstance> tls_;
};

TEST_F(LightStepSinkTest, InitializeSink) {
  {
    std::string invalid_config = R"EOF(
      {"fake" : "fake"}
    )EOF";
    Json::StringLoader loader(invalid_config);

    EXPECT_THROW(setup(loader, false), EnvoyException);
  }

  {
    std::string empty_config = "{}";
    Json::StringLoader loader(empty_config);

    EXPECT_THROW(setup(loader, false), EnvoyException);
  }

  {
    // Valid config but not valid cluster.
    EXPECT_CALL(cm_, get("lightstep_saas")).WillOnce(Return(nullptr));

    std::string valid_config = R"EOF(
      {"collector_cluster": "lightstep_saas"}
    )EOF";
    Json::StringLoader loader(valid_config);

    EXPECT_THROW(setup(loader, false), EnvoyException);
  }

  {
    // Valid config, but upstream cluster does not support http2.
    EXPECT_CALL(cm_, get("lightstep_saas")).WillRepeatedly(Return(&cluster_));
    ON_CALL(cluster_, features()).WillByDefault(Return(0));

    std::string valid_config = R"EOF(
      {"collector_cluster": "lightstep_saas"}
    )EOF";
    Json::StringLoader loader(valid_config);

    EXPECT_THROW(setup(loader, false), EnvoyException);
  }

  {
    EXPECT_CALL(cm_, get("lightstep_saas")).WillRepeatedly(Return(&cluster_));
    ON_CALL(cluster_, features()).WillByDefault(Return(Upstream::Cluster::Features::HTTP2));

    std::string valid_config = R"EOF(
      {"collector_cluster": "lightstep_saas"}
    )EOF";
    Json::StringLoader loader(valid_config);

    setup(loader, true);
  }
}

TEST_F(LightStepSinkTest, FlushSeveralSpans) {
  setupValidSink();

  NiceMock<Http::AccessLog::MockRequestInfo> request_info;
  Http::MockAsyncClientRequest request(&cm_.async_client_);
  Http::AsyncClient::Callbacks* callback;
  const Optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));

  EXPECT_CALL(cm_.async_client_, send_(_, _, timeout))
      .WillOnce(
          Invoke([&](Http::MessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                     const Optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            EXPECT_EQ("/lightstep.collector.CollectorService/Report",
                      message->headers().get(Http::Headers::get().Path));
            EXPECT_EQ("lightstep_saas", message->headers().get(Http::Headers::get().Host));
            EXPECT_EQ("application/grpc", message->headers().get(Http::Headers::get().ContentType));

            return &request;
          }));

  SystemTime start_time;
  EXPECT_CALL(request_info, startTime()).Times(2).WillRepeatedly(Return(start_time));
  Optional<uint32_t> code(200);
  EXPECT_CALL(request_info, responseCode()).WillRepeatedly(ReturnRef(code));

  const std::string protocol = "http/1";
  EXPECT_CALL(request_info, protocol()).Times(2).WillRepeatedly(ReturnRef(protocol));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.min_flush_spans", 5))
      .Times(2)
      .WillRepeatedly(Return(2));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.request_timeout", 5000U))
      .WillOnce(Return(5000U));

  sink_->flushTrace(empty_header_, response_headers_, request_info);
  sink_->flushTrace(empty_header_, response_headers_, request_info);

  Http::MessagePtr msg(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::HeaderMapImpl{{":status", "200"}}}));

  msg->trailers(std::move(Http::HeaderMapPtr{new Http::HeaderMapImpl{{"grpc-status", "0"}}}));

  callback->onSuccess(std::move(msg));

  EXPECT_EQ(
      1U,
      stats_.counter(
                 "cluster.lightstep_saas.grpc.lightstep.collector.CollectorService.Report.success")
          .value());

  callback->onFailure(Http::AsyncClient::FailureReason::Reset);

  EXPECT_EQ(
      1U,
      stats_.counter(
                 "cluster.lightstep_saas.grpc.lightstep.collector.CollectorService.Report.failure")
          .value());

  EXPECT_EQ(
      2U,
      stats_.counter(
                 "cluster.lightstep_saas.grpc.lightstep.collector.CollectorService.Report.total")
          .value());

  EXPECT_EQ(2U, stats_.counter("tracing.lightstep.spans_sent").value());
}

TEST_F(LightStepSinkTest, FlushSpansTimer) {
  setupValidSink();

  NiceMock<Http::AccessLog::MockRequestInfo> request_info;

  const Optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));
  EXPECT_CALL(cm_.async_client_, send_(_, _, timeout));

  SystemTime start_time;
  EXPECT_CALL(request_info, startTime()).WillOnce(Return(start_time));
  Optional<uint32_t> code(200);
  EXPECT_CALL(request_info, responseCode()).WillRepeatedly(ReturnRef(code));

  const std::string protocol = "http/1";
  EXPECT_CALL(request_info, protocol()).WillOnce(ReturnRef(protocol));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.min_flush_spans", 5))
      .WillOnce(Return(5));

  sink_->flushTrace(empty_header_, response_headers_, request_info);
  // Timer should be re-enabled.
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(1000)));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.request_timeout", 5000U))
      .WillOnce(Return(5000U));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.flush_interval_ms", 1000U))
      .WillOnce(Return(1000U));

  timer_->callback_();

  EXPECT_EQ(1U, stats_.counter("tracing.lightstep.timer_flushed").value());
  EXPECT_EQ(1U, stats_.counter("tracing.lightstep.spans_sent").value());
}

TEST_F(LightStepSinkTest, FlushOneSpanGrpcFailure) {
  setupValidSink();

  NiceMock<Http::AccessLog::MockRequestInfo> request_info;
  Http::MockAsyncClientRequest request(&cm_.async_client_);
  Http::AsyncClient::Callbacks* callback;
  const Optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));

  EXPECT_CALL(cm_.async_client_, send_(_, _, timeout))
      .WillOnce(
          Invoke([&](Http::MessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                     const Optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            EXPECT_EQ("/lightstep.collector.CollectorService/Report",
                      message->headers().get(Http::Headers::get().Path));
            EXPECT_EQ("lightstep_saas", message->headers().get(Http::Headers::get().Host));
            EXPECT_EQ("application/grpc", message->headers().get(Http::Headers::get().ContentType));

            return &request;
          }));

  SystemTime start_time;
  EXPECT_CALL(request_info, startTime()).WillOnce(Return(start_time));
  Optional<uint32_t> code(200);
  EXPECT_CALL(request_info, responseCode()).WillRepeatedly(ReturnRef(code));

  const std::string protocol = "http/1";
  EXPECT_CALL(request_info, protocol()).WillOnce(ReturnRef(protocol));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.min_flush_spans", 5))
      .WillOnce(Return(1));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.request_timeout", 5000U))
      .WillOnce(Return(5000U));

  sink_->flushTrace(empty_header_, response_headers_, request_info);

  Http::MessagePtr msg(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::HeaderMapImpl{{":status", "200"}}}));

  // No trailers, gRPC is considered failed.
  callback->onSuccess(std::move(msg));

  EXPECT_EQ(
      1U,
      stats_.counter(
                 "cluster.lightstep_saas.grpc.lightstep.collector.CollectorService.Report.failure")
          .value());

  EXPECT_EQ(
      1U,
      stats_.counter(
                 "cluster.lightstep_saas.grpc.lightstep.collector.CollectorService.Report.total")
          .value());
  EXPECT_EQ(1U, stats_.counter("tracing.lightstep.spans_sent").value());
}

} // Tracing
