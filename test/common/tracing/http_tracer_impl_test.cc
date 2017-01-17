#include "common/http/headers.h"
#include "common/http/header_map_impl.h"
#include "common/http/message_impl.h"
#include "common/runtime/runtime_impl.h"
#include "common/runtime/uuid_util.h"
#include "common/tracing/http_tracer_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

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

    Http::TestHeaderMapImpl request_headers{
        {"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"}};
    HttpTracerUtility::mutateHeaders(request_headers, runtime);

    EXPECT_EQ(UuidTraceStatus::Sampled,
              UuidUtils::isTraceableUuid(request_headers.get_("x-request-id")));
  }

  // Sampling must not be done on client traced.
  {
    NiceMock<Runtime::MockLoader> runtime;
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.random_sampling", 0, _, 10000)).Times(0);
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(true));

    Http::TestHeaderMapImpl request_headers{
        {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}};
    HttpTracerUtility::mutateHeaders(request_headers, runtime);

    EXPECT_EQ(UuidTraceStatus::Forced,
              UuidUtils::isTraceableUuid(request_headers.get_("x-request-id")));
  }

  // Sampling, global off.
  {
    NiceMock<Runtime::MockLoader> runtime;
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.random_sampling", 0, _, 10000))
        .WillOnce(Return(true));
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(false));

    Http::TestHeaderMapImpl request_headers{
        {"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"}};
    HttpTracerUtility::mutateHeaders(request_headers, runtime);

    EXPECT_EQ(UuidTraceStatus::NoTrace,
              UuidUtils::isTraceableUuid(request_headers.get_("x-request-id")));
  }

  // Client, client enabled, global on.
  {
    NiceMock<Runtime::MockLoader> runtime;
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.client_enabled", 100))
        .WillOnce(Return(true));
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(true));

    Http::TestHeaderMapImpl request_headers{
        {"x-client-trace-id", "f4dca0a9-12c7-4307-8002-969403baf480"},
        {"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"}};
    HttpTracerUtility::mutateHeaders(request_headers, runtime);

    EXPECT_EQ(UuidTraceStatus::Client,
              UuidUtils::isTraceableUuid(request_headers.get_("x-request-id")));
  }

  // Client, client disabled, global on.
  {
    NiceMock<Runtime::MockLoader> runtime;
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.client_enabled", 100))
        .WillOnce(Return(false));
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(true));

    Http::TestHeaderMapImpl request_headers{
        {"x-client-trace-id", "f4dca0a9-12c7-4307-8002-969403baf480"},
        {"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"}};
    HttpTracerUtility::mutateHeaders(request_headers, runtime);

    EXPECT_EQ(UuidTraceStatus::NoTrace,
              UuidUtils::isTraceableUuid(request_headers.get_("x-request-id")));
  }

  // Forced, global on.
  {
    NiceMock<Runtime::MockLoader> runtime;
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(true));

    Http::TestHeaderMapImpl request_headers{
        {"x-envoy-force-trace", "true"}, {"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"}};
    HttpTracerUtility::mutateHeaders(request_headers, runtime);

    EXPECT_EQ(UuidTraceStatus::Forced,
              UuidUtils::isTraceableUuid(request_headers.get_("x-request-id")));
  }

  // Forced, global off.
  {
    NiceMock<Runtime::MockLoader> runtime;
    EXPECT_CALL(runtime.snapshot_, featureEnabled("tracing.global_enabled", 100, _))
        .WillOnce(Return(false));

    Http::TestHeaderMapImpl request_headers{
        {"x-envoy-force-trace", "true"}, {"x-request-id", "125a4afb-6f55-44ba-ad80-413f09f48a28"}};
    HttpTracerUtility::mutateHeaders(request_headers, runtime);

    EXPECT_EQ(UuidTraceStatus::NoTrace,
              UuidUtils::isTraceableUuid(request_headers.get_("x-request-id")));
  }

  // Forced, global on, broken uuid.
  {
    NiceMock<Runtime::MockLoader> runtime;

    Http::TestHeaderMapImpl request_headers{{"x-envoy-force-trace", "true"},
                                            {"x-request-id", "bb"}};
    HttpTracerUtility::mutateHeaders(request_headers, runtime);

    EXPECT_EQ(UuidTraceStatus::NoTrace,
              UuidUtils::isTraceableUuid(request_headers.get_("x-request-id")));
  }
}

TEST(HttpTracerUtilityTest, IsTracing) {
  NiceMock<Http::AccessLog::MockRequestInfo> request_info;
  NiceMock<Stats::MockStore> stats;
  Runtime::RandomGeneratorImpl random;
  std::string not_traceable_guid = random.uuid();

  std::string forced_guid = random.uuid();
  UuidUtils::setTraceableUuid(forced_guid, UuidTraceStatus::Forced);
  Http::TestHeaderMapImpl forced_header{{"x-request-id", forced_guid}};

  std::string sampled_guid = random.uuid();
  UuidUtils::setTraceableUuid(sampled_guid, UuidTraceStatus::Sampled);
  Http::TestHeaderMapImpl sampled_header{{"x-request-id", sampled_guid}};

  std::string client_guid = random.uuid();
  UuidUtils::setTraceableUuid(client_guid, UuidTraceStatus::Client);
  Http::TestHeaderMapImpl client_header{{"x-request-id", client_guid}};

  Http::TestHeaderMapImpl not_traceable_header{{"x-request-id", not_traceable_guid}};
  Http::TestHeaderMapImpl empty_header{};

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
    Http::TestHeaderMapImpl traceable_header_hc{{"x-request-id", forced_guid}};
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

  // No request id.
  {
    Http::TestHeaderMapImpl headers;
    EXPECT_CALL(request_info, healthCheck()).WillOnce(Return(false));
    Decision result = HttpTracerUtility::isTracing(request_info, headers);
    EXPECT_EQ(Reason::NotTraceableRequestId, result.reason);
    EXPECT_FALSE(result.is_tracing);
  }

  // Broken request id.
  {
    Http::TestHeaderMapImpl headers{{"x-request-id", "not-real-x-request-id"}};
    EXPECT_CALL(request_info, healthCheck()).WillOnce(Return(false));
    Decision result = HttpTracerUtility::isTracing(request_info, headers);
    EXPECT_EQ(Reason::NotTraceableRequestId, result.reason);
    EXPECT_FALSE(result.is_tracing);
  }
}

class TracingContextImplTest : public Test {
public:
  TracingContextImplTest() {
    ON_CALL(config_, operationName()).WillByDefault(ReturnRef(operation_));
    ON_CALL(config_, serviceNode()).WillByDefault(ReturnRef(node_));
    ON_CALL(request_info_, startTime()).WillByDefault(Return(start_time_));
  }

  void setup(bool is_null_tracer) {
    if (is_null_tracer) {
      context_.reset(new TracingContextImpl(null_tracer_, config_));
    } else {
      context_.reset(new TracingContextImpl(tracer_, config_));
    }
  }

  NiceMock<MockHttpTracer> tracer_;
  HttpNullTracer null_tracer_;
  NiceMock<MockTracingConfig> config_;
  NiceMock<Http::AccessLog::MockRequestInfo> request_info_;
  Http::TestHeaderMapImpl request_headers_{
      {"x-request-id", "id"}, {":path", "/test"}, {":method", "GET"}};
  Http::TestHeaderMapImpl response_headers_{};
  SystemTime start_time_;
  TracingContextPtr context_;

  const std::string operation_{"operation"};
  const std::string node_{"i-453"};
};

TEST_F(TracingContextImplTest, NullSpanFromTracer) {
  setup(false);
  EXPECT_CALL(config_, operationName());
  EXPECT_CALL(request_info_, startTime());
  EXPECT_CALL(tracer_, startSpan(operation_, start_time_)).WillOnce(Return(nullptr));

  // null span should work fine.
  context_->startSpan(request_info_, request_headers_);
  context_->finishSpan(request_info_, &response_headers_);
}

TEST_F(TracingContextImplTest, NullResponseHeaders) {
  setup(false);
  EXPECT_CALL(config_, operationName());
  EXPECT_CALL(request_info_, startTime());
  EXPECT_CALL(tracer_, startSpan(operation_, start_time_)).WillOnce(Return(nullptr));

  // No response headers.
  context_->startSpan(request_info_, request_headers_);
  context_->finishSpan(request_info_, nullptr);
}

TEST_F(TracingContextImplTest, SpanPopulatedOptionalHeaders) {
  setup(false);
  MockSpan* span = new MockSpan();

  EXPECT_CALL(config_, operationName());
  EXPECT_CALL(request_info_, startTime());
  EXPECT_CALL(request_info_, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(tracer_, startSpan(operation_, start_time_)).WillOnce(Return(SpanPtr{span}));

  // Check that span is populated correctly.
  EXPECT_CALL(*span, setTag("guid:x-request-id", "id"));
  EXPECT_CALL(*span, setTag("node_id", "i-453"));
  EXPECT_CALL(*span, setTag("request_line", "GET /test HTTP/1.0"));
  EXPECT_CALL(*span, setTag("host_header", "-"));
  EXPECT_CALL(*span, setTag("user_agent", "-"));
  EXPECT_CALL(*span, setTag("downstream_cluster", "-"));
  EXPECT_CALL(*span, setTag("request_size", "10"));

  context_->startSpan(request_info_, request_headers_);

  Optional<uint32_t> response_code;
  EXPECT_CALL(request_info_, responseCode()).WillRepeatedly(ReturnRef(response_code));
  EXPECT_CALL(request_info_, bytesSent()).WillOnce(Return(100));

  EXPECT_CALL(*span, setTag("response_code", "0"));
  EXPECT_CALL(*span, setTag("response_size", "100"));
  EXPECT_CALL(*span, setTag("response_flags", "-"));

  EXPECT_CALL(*span, finishSpan());
  context_->finishSpan(request_info_, &response_headers_);
}

TEST_F(TracingContextImplTest, SpanPopulatedFailureResponse) {
  setup(false);
  MockSpan* span = new MockSpan();

  request_headers_.insertHost().value(std::string("api"));
  request_headers_.insertUserAgent().value(std::string("agent"));
  request_headers_.insertEnvoyDownstreamServiceCluster().value(std::string("downstream_cluster"));
  request_headers_.insertClientTraceId().value(std::string("client_trace_id"));

  EXPECT_CALL(config_, operationName());
  EXPECT_CALL(request_info_, startTime());
  EXPECT_CALL(request_info_, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(tracer_, startSpan(operation_, start_time_)).WillOnce(Return(SpanPtr{span}));

  // Check that span is populated correctly.
  EXPECT_CALL(*span, setTag("guid:x-request-id", "id"));
  EXPECT_CALL(*span, setTag("node_id", "i-453"));
  EXPECT_CALL(*span, setTag("request_line", "GET /test HTTP/1.0"));
  EXPECT_CALL(*span, setTag("host_header", "api"));
  EXPECT_CALL(*span, setTag("user_agent", "agent"));
  EXPECT_CALL(*span, setTag("downstream_cluster", "downstream_cluster"));
  EXPECT_CALL(*span, setTag("request_size", "10"));
  EXPECT_CALL(*span, setTag("guid:x-client-trace-id", "client_trace_id"));

  context_->startSpan(request_info_, request_headers_);

  Optional<uint32_t> response_code(500);
  EXPECT_CALL(request_info_, responseCode()).WillRepeatedly(ReturnRef(response_code));
  EXPECT_CALL(request_info_, bytesSent()).WillOnce(Return(100));

  EXPECT_CALL(*span, setTag("error", "true"));
  EXPECT_CALL(*span, setTag("response_code", "500"));
  EXPECT_CALL(*span, setTag("response_size", "100"));
  EXPECT_CALL(*span, setTag("response_flags", "-"));

  EXPECT_CALL(*span, finishSpan());
  context_->finishSpan(request_info_, &response_headers_);
}

TEST_F(TracingContextImplTest, NullHttpTracer) {
  setup(true);
  EXPECT_CALL(config_, operationName());
  EXPECT_CALL(request_info_, startTime());

  context_->startSpan(request_info_, request_headers_);
  context_->finishSpan(request_info_, &response_headers_);
}

class LightStepDriverTest : public Test {
public:
  void setup(Json::Object& config, bool init_timer) {
    std::unique_ptr<lightstep::TracerOptions> opts(new lightstep::TracerOptions());
    opts->access_token = "sample_token";
    opts->tracer_attributes["lightstep.component_name"] = "component";

    ON_CALL(cm_, httpAsyncClientForCluster("fake_cluster"))
        .WillByDefault(ReturnRef(cm_.async_client_));

    if (init_timer) {
      timer_ = new NiceMock<Event::MockTimer>(&tls_.dispatcher_);
      EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(1000)));
    }

    driver_.reset(new LightStepDriver(config, cm_, stats_, tls_, runtime_, std::move(opts)));
  }

  void setupValidDriver() {
    EXPECT_CALL(cm_, get("fake_cluster")).WillRepeatedly(Return(cm_.cluster_.info_));
    ON_CALL(*cm_.cluster_.info_, features())
        .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));

    std::string valid_config = R"EOF(
      {"collector_cluster": "fake_cluster"}
    )EOF";
    Json::ObjectPtr loader = Json::Factory::LoadFromString(valid_config);

    setup(*loader, true);
  }

  const std::string operation_name_{"test"};
  const Http::TestHeaderMapImpl request_headers_{
      {":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}};
  const Http::TestHeaderMapImpl response_headers_{{":status", "500"}};

  std::unique_ptr<LightStepDriver> driver_;
  NiceMock<Event::MockTimer>* timer_;
  Stats::IsolatedStoreImpl stats_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
};

TEST_F(LightStepDriverTest, InitializeDriver) {
  {
    std::string invalid_config = R"EOF(
      {"fake" : "fake"}
    )EOF";
    Json::ObjectPtr loader = Json::Factory::LoadFromString(invalid_config);

    EXPECT_THROW(setup(*loader, false), EnvoyException);
  }

  {
    std::string empty_config = "{}";
    Json::ObjectPtr loader = Json::Factory::LoadFromString(empty_config);

    EXPECT_THROW(setup(*loader, false), EnvoyException);
  }

  {
    // Valid config but not valid cluster.
    EXPECT_CALL(cm_, get("fake_cluster")).WillOnce(Return(nullptr));

    std::string valid_config = R"EOF(
      {"collector_cluster": "fake_cluster"}
    )EOF";
    Json::ObjectPtr loader = Json::Factory::LoadFromString(valid_config);

    EXPECT_THROW(setup(*loader, false), EnvoyException);
  }

  {
    // Valid config, but upstream cluster does not support http2.
    EXPECT_CALL(cm_, get("fake_cluster")).WillRepeatedly(Return(cm_.cluster_.info_));
    ON_CALL(*cm_.cluster_.info_, features()).WillByDefault(Return(0));

    std::string valid_config = R"EOF(
      {"collector_cluster": "fake_cluster"}
    )EOF";
    Json::ObjectPtr loader = Json::Factory::LoadFromString(valid_config);

    EXPECT_THROW(setup(*loader, false), EnvoyException);
  }

  {
    EXPECT_CALL(cm_, get("fake_cluster")).WillRepeatedly(Return(cm_.cluster_.info_));
    ON_CALL(*cm_.cluster_.info_, features())
        .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));

    std::string valid_config = R"EOF(
      {"collector_cluster": "fake_cluster"}
    )EOF";
    Json::ObjectPtr loader = Json::Factory::LoadFromString(valid_config);

    setup(*loader, true);
  }
}

TEST_F(LightStepDriverTest, FlushSeveralSpans) {
  setupValidDriver();

  Http::MockAsyncClientRequest request(&cm_.async_client_);
  Http::AsyncClient::Callbacks* callback;
  const Optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));

  EXPECT_CALL(cm_.async_client_, send_(_, _, timeout))
      .WillOnce(
          Invoke([&](Http::MessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                     const Optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            EXPECT_STREQ("/lightstep.collector.CollectorService/Report",
                         message->headers().Path()->value().c_str());
            EXPECT_STREQ("fake_cluster", message->headers().Host()->value().c_str());
            EXPECT_STREQ("application/grpc", message->headers().ContentType()->value().c_str());

            return &request;
          }));

  SystemTime start_time;
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.min_flush_spans", 5))
      .Times(2)
      .WillRepeatedly(Return(2));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.request_timeout", 5000U))
      .WillOnce(Return(5000U));

  SpanPtr first_span = driver_->startSpan(operation_name_, start_time);
  first_span->finishSpan();
  SpanPtr second_span = driver_->startSpan(operation_name_, start_time);
  second_span->finishSpan();

  Http::MessagePtr msg(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));

  msg->trailers(std::move(Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{"grpc-status", "0"}}}));

  callback->onSuccess(std::move(msg));

  EXPECT_EQ(1U, cm_.cluster_.info_->stats_store_
                    .counter("grpc.lightstep.collector.CollectorService.Report.success")
                    .value());

  callback->onFailure(Http::AsyncClient::FailureReason::Reset);

  EXPECT_EQ(1U, cm_.cluster_.info_->stats_store_
                    .counter("grpc.lightstep.collector.CollectorService.Report.failure")
                    .value());
  EXPECT_EQ(2U, cm_.cluster_.info_->stats_store_
                    .counter("grpc.lightstep.collector.CollectorService.Report.total")
                    .value());
  EXPECT_EQ(2U, stats_.counter("tracing.lightstep.spans_sent").value());
}

TEST_F(LightStepDriverTest, FlushSpansTimer) {
  setupValidDriver();

  const Optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));
  EXPECT_CALL(cm_.async_client_, send_(_, _, timeout));

  SystemTime start_time;
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.min_flush_spans", 5))
      .WillOnce(Return(5));

  SpanPtr span = driver_->startSpan(operation_name_, start_time);
  span->finishSpan();

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

TEST_F(LightStepDriverTest, FlushOneSpanGrpcFailure) {
  setupValidDriver();

  Http::MockAsyncClientRequest request(&cm_.async_client_);
  Http::AsyncClient::Callbacks* callback;
  const Optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));

  EXPECT_CALL(cm_.async_client_, send_(_, _, timeout))
      .WillOnce(
          Invoke([&](Http::MessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                     const Optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            EXPECT_STREQ("/lightstep.collector.CollectorService/Report",
                         message->headers().Path()->value().c_str());
            EXPECT_STREQ("fake_cluster", message->headers().Host()->value().c_str());
            EXPECT_STREQ("application/grpc", message->headers().ContentType()->value().c_str());

            return &request;
          }));

  SystemTime start_time;

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.min_flush_spans", 5))
      .WillOnce(Return(1));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.request_timeout", 5000U))
      .WillOnce(Return(5000U));

  SpanPtr span = driver_->startSpan(operation_name_, start_time);
  span->finishSpan();

  Http::MessagePtr msg(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));

  // No trailers, gRPC is considered failed.
  callback->onSuccess(std::move(msg));

  EXPECT_EQ(1U, cm_.cluster_.info_->stats_store_
                    .counter("grpc.lightstep.collector.CollectorService.Report.failure")
                    .value());
  EXPECT_EQ(1U, cm_.cluster_.info_->stats_store_
                    .counter("grpc.lightstep.collector.CollectorService.Report.total")
                    .value());
  EXPECT_EQ(1U, stats_.counter("tracing.lightstep.spans_sent").value());
}
} // Tracing
