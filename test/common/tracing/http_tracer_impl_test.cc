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

TEST(HttpTracerUtilityTest, OriginalAndLongPath) {
  const std::string path(300, 'a');
  const std::string expected_path(256, 'a');
  std::unique_ptr<NiceMock<MockSpan>> span(new NiceMock<MockSpan>());

  Http::TestHeaderMapImpl request_headers{
      {"x-request-id", "id"}, {"x-envoy-original-path", path}, {":method", "GET"}};
  NiceMock<Http::AccessLog::MockRequestInfo> request_info;

  Http::Protocol protocol = Http::Protocol::Http2;
  EXPECT_CALL(request_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(request_info, protocol()).WillOnce(Return(protocol));
  const std::string service_node = "node";

  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(*span, setTag("request_line", "GET " + expected_path + " HTTP/2"));
  HttpTracerUtility::populateSpan(*span, service_node, request_headers, request_info);
}

TEST(HttpTracerUtilityTest, SpanOptionalHeaders) {
  std::unique_ptr<NiceMock<MockSpan>> span(new NiceMock<MockSpan>());

  Http::TestHeaderMapImpl request_headers{
      {"x-request-id", "id"}, {":path", "/test"}, {":method", "GET"}};
  NiceMock<Http::AccessLog::MockRequestInfo> request_info;

  Http::Protocol protocol = Http::Protocol::Http10;
  EXPECT_CALL(request_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(request_info, protocol()).WillOnce(Return(protocol));
  const std::string service_node = "i-453";

  // Check that span is populated correctly.
  EXPECT_CALL(*span, setTag("guid:x-request-id", "id"));
  EXPECT_CALL(*span, setTag("node_id", service_node));
  EXPECT_CALL(*span, setTag("request_line", "GET /test HTTP/1.0"));
  EXPECT_CALL(*span, setTag("host_header", "-"));
  EXPECT_CALL(*span, setTag("user_agent", "-"));
  EXPECT_CALL(*span, setTag("downstream_cluster", "-"));
  EXPECT_CALL(*span, setTag("request_size", "10"));

  HttpTracerUtility::populateSpan(*span, service_node, request_headers, request_info);

  Optional<uint32_t> response_code;
  EXPECT_CALL(request_info, responseCode()).WillRepeatedly(ReturnRef(response_code));
  EXPECT_CALL(request_info, bytesSent()).WillOnce(Return(100));

  EXPECT_CALL(*span, setTag("response_code", "0"));
  EXPECT_CALL(*span, setTag("response_size", "100"));
  EXPECT_CALL(*span, setTag("response_flags", "-"));

  EXPECT_CALL(*span, finishSpan());
  HttpTracerUtility::finalizeSpan(*span, request_info);
}

TEST(HttpTracerUtilityTest, SpanPopulatedFailureResponse) {
  std::unique_ptr<NiceMock<MockSpan>> span(new NiceMock<MockSpan>());
  Http::TestHeaderMapImpl request_headers{
      {"x-request-id", "id"}, {":path", "/test"}, {":method", "GET"}};
  NiceMock<Http::AccessLog::MockRequestInfo> request_info;
  Http::Protocol protocol = Http::Protocol::Http10;
  EXPECT_CALL(request_info, protocol()).WillOnce(Return(protocol));

  request_headers.insertHost().value(std::string("api"));
  request_headers.insertUserAgent().value(std::string("agent"));
  request_headers.insertEnvoyDownstreamServiceCluster().value(std::string("downstream_cluster"));
  request_headers.insertClientTraceId().value(std::string("client_trace_id"));

  EXPECT_CALL(request_info, bytesReceived()).WillOnce(Return(10));
  const std::string service_node = "i-453";

  // Check that span is populated correctly.
  EXPECT_CALL(*span, setTag("guid:x-request-id", "id"));
  EXPECT_CALL(*span, setTag("node_id", service_node));
  EXPECT_CALL(*span, setTag("request_line", "GET /test HTTP/1.0"));
  EXPECT_CALL(*span, setTag("host_header", "api"));
  EXPECT_CALL(*span, setTag("user_agent", "agent"));
  EXPECT_CALL(*span, setTag("downstream_cluster", "downstream_cluster"));
  EXPECT_CALL(*span, setTag("request_size", "10"));
  EXPECT_CALL(*span, setTag("guid:x-client-trace-id", "client_trace_id"));

  HttpTracerUtility::populateSpan(*span, service_node, request_headers, request_info);

  Optional<uint32_t> response_code(503);
  EXPECT_CALL(request_info, responseCode()).WillRepeatedly(ReturnRef(response_code));
  EXPECT_CALL(request_info, bytesSent()).WillOnce(Return(100));
  ON_CALL(request_info, getResponseFlag(Http::AccessLog::ResponseFlag::UpstreamRequestTimeout))
      .WillByDefault(Return(true));

  EXPECT_CALL(*span, setTag("error", "true"));
  EXPECT_CALL(*span, setTag("response_code", "503"));
  EXPECT_CALL(*span, setTag("response_size", "100"));
  EXPECT_CALL(*span, setTag("response_flags", "UT"));

  EXPECT_CALL(*span, finishSpan());
  HttpTracerUtility::finalizeSpan(*span, request_info);
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
  Http::TestHeaderMapImpl request_headers_{
      {":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}};
  const Http::TestHeaderMapImpl response_headers_{{":status", "500"}};
  SystemTime start_time_;

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

TEST(HttpNullTracerTest, BasicFunctionality) {
  HttpNullTracer null_tracer;
  MockConfig config;
  Http::AccessLog::MockRequestInfo request_info;
  Http::TestHeaderMapImpl request_headers;

  EXPECT_EQ(nullptr, null_tracer.startSpan(config, request_headers, request_info));
}

TEST(HttpTracerImplTest, BasicFunctionalityNullSpan) {
  LocalInfo::MockLocalInfo local_info;
  SystemTime time;
  Http::AccessLog::MockRequestInfo request_info;
  EXPECT_CALL(request_info, startTime()).WillOnce(Return(time));

  MockConfig config;
  const std::string operation_name = "operation";
  EXPECT_CALL(config, operationName()).WillOnce(ReturnRef(operation_name));

  Http::TestHeaderMapImpl request_headers;
  MockDriver* driver = new MockDriver();
  DriverPtr driver_ptr(driver);

  HttpTracerImpl tracer(std::move(driver_ptr), local_info);

  EXPECT_CALL(*driver, startSpan_(_, operation_name, time)).WillOnce(Return(nullptr));

  tracer.startSpan(config, request_headers, request_info);
}

TEST(HttpTracerImplTest, BasicFunctionalityNodeSet) {
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  SystemTime time;
  NiceMock<Http::AccessLog::MockRequestInfo> request_info;
  EXPECT_CALL(request_info, startTime()).WillOnce(Return(time));
  EXPECT_CALL(local_info, nodeName());

  MockConfig config;
  const std::string operation_name = "operation";
  EXPECT_CALL(config, operationName()).WillOnce(ReturnRef(operation_name));

  Http::TestHeaderMapImpl request_headers{
      {"x-request-id", "id"}, {":path", "/test"}, {":method", "GET"}};
  MockDriver* driver = new MockDriver();
  DriverPtr driver_ptr(driver);

  HttpTracerImpl tracer(std::move(driver_ptr), local_info);

  NiceMock<MockSpan>* span = new NiceMock<MockSpan>();
  EXPECT_CALL(*driver, startSpan_(_, operation_name, time)).WillOnce(Return(span));

  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(*span, setTag("node_id", "node_name"));
  tracer.startSpan(config, request_headers, request_info);
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

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.min_flush_spans", 5))
      .Times(2)
      .WillRepeatedly(Return(2));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.request_timeout", 5000U))
      .WillOnce(Return(5000U));

  Http::TestHeaderMapImpl headers;
  SpanPtr first_span = driver_->startSpan(headers, operation_name_, start_time_);
  first_span->finishSpan();

  SpanPtr second_span = driver_->startSpan(headers, operation_name_, start_time_);
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

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.min_flush_spans", 5))
      .WillOnce(Return(5));

  SpanPtr span = driver_->startSpan(request_headers_, operation_name_, start_time_);
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
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.min_flush_spans", 5))
      .WillOnce(Return(1));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.request_timeout", 5000U))
      .WillOnce(Return(5000U));

  Http::TestHeaderMapImpl headers;
  SpanPtr span = driver_->startSpan(headers, operation_name_, start_time_);
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

TEST_F(LightStepDriverTest, SerializeAndDeserializeContext) {
  setupValidDriver();

  // Supply bogus context, that will be simply ignored.
  const std::string invalid_context = "not valid context";
  request_headers_.insertOtSpanContext().value(invalid_context);
  driver_->startSpan(request_headers_, operation_name_, start_time_);

  std::string injected_ctx = request_headers_.OtSpanContext()->value().c_str();
  EXPECT_FALSE(injected_ctx.empty());

  // Supply empty context.
  request_headers_.removeOtSpanContext();
  SpanPtr span = driver_->startSpan(request_headers_, operation_name_, start_time_);

  injected_ctx = request_headers_.OtSpanContext()->value().c_str();
  EXPECT_FALSE(injected_ctx.empty());

  // Context can be parsed fine.
  lightstep::envoy::CarrierStruct ctx;
  ctx.ParseFromString(injected_ctx);

  // Supply parent context, request_headers has properly populated x-ot-span-context.
  SpanPtr span_with_parent = driver_->startSpan(request_headers_, operation_name_, start_time_);
  injected_ctx = request_headers_.OtSpanContext()->value().c_str();
  EXPECT_FALSE(injected_ctx.empty());
}

} // Tracing
