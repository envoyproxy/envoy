#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "envoy/config/trace/v3/zipkin.pb.h"

#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/extensions/tracers/zipkin/zipkin_core_constants.h"
#include "source/extensions/tracers/zipkin/zipkin_tracer_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::DoAll;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::StrictMock;
using testing::WithArg;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {
namespace {

class ZipkinDriverTest : public testing::Test {
public:
  ZipkinDriverTest() : time_source_(test_time_.timeSystem()) {}

  void setup(envoy::config::trace::v3::ZipkinConfig& zipkin_config, bool init_timer) {
    cm_.thread_local_cluster_.cluster_.info_->name_ = "fake_cluster";
    cm_.initializeThreadLocalClusters({"fake_cluster"});
    ON_CALL(cm_.thread_local_cluster_, httpAsyncClient())
        .WillByDefault(ReturnRef(cm_.thread_local_cluster_.async_client_));

    if (init_timer) {
      timer_ = new NiceMock<Event::MockTimer>(&tls_.dispatcher_);
      EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5000), _));
    }

    driver_ = std::make_unique<Driver>(zipkin_config, context_);
  }

  void setupValidDriverWithHostname(const std::string& version, const std::string& hostname) {
    cm_.initializeClusters({"fake_cluster"}, {});

    std::string yaml_string = fmt::format(R"EOF(
    collector_cluster: fake_cluster
    collector_endpoint: /api/v2/spans
    collector_endpoint_version: {}
    )EOF",
                                          version);
    if (!hostname.empty()) {
      yaml_string = yaml_string + fmt::format(R"EOF(
    collector_hostname: {}
    )EOF",
                                              hostname);
    }

    envoy::config::trace::v3::ZipkinConfig zipkin_config;
    TestUtility::loadFromYaml(yaml_string, zipkin_config);

    setup(zipkin_config, true);
  }

  void expectValidFlushSeveralSpansWithHostname(const std::string& version,
                                                const std::string& content_type,
                                                const std::string& hostname) {
    setupValidDriverWithHostname(version, hostname);

    Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
    Http::AsyncClient::Callbacks* callback;
    const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));

    EXPECT_CALL(cm_.thread_local_cluster_.async_client_,
                send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(timeout)))
        .WillOnce(
            Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                       const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
              callback = &callbacks;

              const std::string& expected_hostname = !hostname.empty() ? hostname : "fake_cluster";
              EXPECT_EQ("/api/v2/spans", message->headers().getPathValue());
              EXPECT_EQ(expected_hostname, message->headers().getHostValue());
              EXPECT_EQ(content_type, message->headers().getContentTypeValue());

              return &request;
            }));

    EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.min_flush_spans", 5))
        .Times(2)
        .WillRepeatedly(Return(2));
    EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.request_timeout", 5000U))
        .WillOnce(Return(5000U));

    Tracing::SpanPtr first_span =
        driver_->startSpan(config_, request_headers_, stream_info_, operation_name_,
                           {Tracing::Reason::Sampling, true});
    first_span->finishSpan();

    Tracing::SpanPtr second_span =
        driver_->startSpan(config_, request_headers_, stream_info_, operation_name_,
                           {Tracing::Reason::Sampling, true});
    second_span->finishSpan();

    Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
        Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "202"}}}));

    callback->onSuccess(request, std::move(msg));

    EXPECT_EQ(2U, stats_.counter("tracing.zipkin.spans_sent").value());
    EXPECT_EQ(0U, stats_.counter("tracing.zipkin.reports_skipped_no_cluster").value());
    EXPECT_EQ(1U, stats_.counter("tracing.zipkin.reports_sent").value());
    EXPECT_EQ(0U, stats_.counter("tracing.zipkin.reports_dropped").value());
    EXPECT_EQ(0U, stats_.counter("tracing.zipkin.reports_failed").value());

    callback->onFailure(request, Http::AsyncClient::FailureReason::Reset);

    EXPECT_EQ(1U, stats_.counter("tracing.zipkin.reports_failed").value());
  }

  void setupValidDriver(const std::string& version) { setupValidDriverWithHostname(version, ""); }

  void expectValidFlushSeveralSpans(const std::string& version, const std::string& content_type) {
    expectValidFlushSeveralSpansWithHostname(version, content_type, "");
  }

  // TODO(#4160): Currently time_system_ is initialized from DangerousDeprecatedTestTime, which uses
  // real time, not mock-time. When that is switched to use mock-time instead, I think
  // generateRandom64() may not be as random as we want, and we'll need to inject entropy
  // appropriate for the test.
  uint64_t generateRandom64() { return Util::generateRandom64(time_source_); }

  const std::string operation_name_{"test"};
  Tracing::TestTraceContextImpl request_headers_{
      {":authority", "api.lyft.com"}, {":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}};
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;

  std::unique_ptr<Driver> driver_;
  NiceMock<Event::MockTimer>* timer_;

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<ThreadLocal::MockInstance>& tls_{context_.thread_local_};
  NiceMock<Stats::MockIsolatedStatsStore>& stats_{context_.store_};
  NiceMock<Upstream::MockClusterManager>& cm_{context_.cluster_manager_};
  NiceMock<Runtime::MockLoader>& runtime_{context_.runtime_loader_};
  NiceMock<LocalInfo::MockLocalInfo>& local_info_{context_.local_info_};
  NiceMock<Random::MockRandomGenerator>& random_{context_.api_.random_};

  NiceMock<Tracing::MockConfig> config_;
  Event::SimulatedTimeSystem test_time_;
  TimeSource& time_source_;
};

TEST_F(ZipkinDriverTest, InitializeDriver) {
  {
    // Empty config
    envoy::config::trace::v3::ZipkinConfig zipkin_config;

    EXPECT_THROW(setup(zipkin_config, false), EnvoyException);
  }

  {
    // Valid config but collector cluster doesn't exists.
    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    collector_endpoint: /api/v2/spans
    collector_endpoint_version: HTTP_JSON
    )EOF";
    envoy::config::trace::v3::ZipkinConfig zipkin_config;
    TestUtility::loadFromYaml(yaml_string, zipkin_config);

    EXPECT_THROW(setup(zipkin_config, false), EnvoyException);
  }

  {
    // valid config
    cm_.initializeClusters({"fake_cluster"}, {});
    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    collector_endpoint: /api/v2/spans
    collector_endpoint_version: HTTP_JSON
    )EOF";
    envoy::config::trace::v3::ZipkinConfig zipkin_config;
    TestUtility::loadFromYaml(yaml_string, zipkin_config);

    setup(zipkin_config, true);
  }
}

TEST_F(ZipkinDriverTest, TraceContextOptionConfiguration) {
  cm_.initializeClusters({"fake_cluster"}, {});

  {
    // Test default trace_context_option value (USE_B3) - W3C fallback should be disabled.
    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    collector_endpoint: /api/v2/spans
    collector_endpoint_version: HTTP_JSON
    )EOF";
    envoy::config::trace::v3::ZipkinConfig zipkin_config;
    TestUtility::loadFromYaml(yaml_string, zipkin_config);

    setup(zipkin_config, true);
    EXPECT_FALSE(driver_->w3cFallbackEnabled()); // W3C fallback should be disabled by default
    EXPECT_EQ(driver_->traceContextOption(), envoy::config::trace::v3::ZipkinConfig::USE_B3);
  }

  {
    // Test trace_context_option explicitly set to USE_B3 - W3C fallback should be disabled.
    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    collector_endpoint: /api/v2/spans
    collector_endpoint_version: HTTP_JSON
    trace_context_option: USE_B3
    )EOF";
    envoy::config::trace::v3::ZipkinConfig zipkin_config;
    TestUtility::loadFromYaml(yaml_string, zipkin_config);

    setup(zipkin_config, true);
    EXPECT_FALSE(driver_->w3cFallbackEnabled()); // W3C fallback should be disabled
    EXPECT_EQ(driver_->traceContextOption(), envoy::config::trace::v3::ZipkinConfig::USE_B3);
  }

  {
    // Test trace_context_option set to USE_B3_WITH_W3C_PROPAGATION - W3C fallback should be
    // enabled.
    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    collector_endpoint: /api/v2/spans
    collector_endpoint_version: HTTP_JSON
    trace_context_option: USE_B3_WITH_W3C_PROPAGATION
    )EOF";
    envoy::config::trace::v3::ZipkinConfig zipkin_config;
    TestUtility::loadFromYaml(yaml_string, zipkin_config);

    setup(zipkin_config, true);
    EXPECT_TRUE(driver_->w3cFallbackEnabled()); // W3C fallback should be enabled
    EXPECT_EQ(driver_->traceContextOption(),
              envoy::config::trace::v3::ZipkinConfig::USE_B3_WITH_W3C_PROPAGATION);
  }
}

TEST_F(ZipkinDriverTest, DualHeaderExtractionAndInjection) {
  cm_.initializeClusters({"fake_cluster"}, {});

  // Test complete dual header cycle: extract from B3 headers, then inject both B3 and W3C headers
  const std::string yaml_string = R"EOF(
  collector_cluster: fake_cluster
  collector_endpoint: /api/v2/spans
  collector_endpoint_version: HTTP_JSON
  trace_context_option: USE_B3_WITH_W3C_PROPAGATION
  )EOF";
  envoy::config::trace::v3::ZipkinConfig zipkin_config;
  TestUtility::loadFromYaml(yaml_string, zipkin_config);

  setup(zipkin_config, true);

  // Step 1: Simulate incoming request with B3 headers (extraction phase)
  Tracing::TestTraceContextImpl incoming_trace_context{
      {"x-b3-traceid", "463ac35c9f6413ad48485a3953bb6124"},
      {"x-b3-spanid", "a2fb4a1d1a96d312"},
      {"x-b3-sampled", "1"}};

  // Create a span from the incoming B3 headers
  Tracing::SpanPtr span = driver_->startSpan(config_, incoming_trace_context, stream_info_,
                                             "test_operation", {Tracing::Reason::Sampling, true});

  // Step 2: Inject context for outgoing request (injection phase)
  Tracing::TestTraceContextImpl outgoing_trace_context{{}};
  Tracing::UpstreamContext upstream_context;
  span->injectContext(outgoing_trace_context, upstream_context);

  // Step 3: Verify both B3 and W3C headers are injected

  // Verify B3 headers are injected
  auto b3_traceid = outgoing_trace_context.get("x-b3-traceid");
  auto b3_spanid = outgoing_trace_context.get("x-b3-spanid");
  auto b3_sampled = outgoing_trace_context.get("x-b3-sampled");

  EXPECT_TRUE(b3_traceid.has_value());
  EXPECT_TRUE(b3_spanid.has_value());
  EXPECT_TRUE(b3_sampled.has_value());

  // Verify the trace ID is preserved from extraction
  EXPECT_EQ(b3_traceid.value(), "463ac35c9f6413ad48485a3953bb6124");
  EXPECT_EQ(b3_sampled.value(), "1");

  // Verify W3C traceparent header is also injected
  auto traceparent = outgoing_trace_context.get("traceparent");
  EXPECT_TRUE(traceparent.has_value());
  EXPECT_FALSE(traceparent.value().empty());

  // Verify traceparent format and contains the same trace ID
  const std::string traceparent_value = std::string(traceparent.value());
  EXPECT_EQ(traceparent_value.length(), 55);                                      // 2+1+32+1+16+1+2
  EXPECT_EQ(traceparent_value.substr(0, 3), "00-");                               // version
  EXPECT_EQ(traceparent_value.substr(3, 32), "463ac35c9f6413ad48485a3953bb6124"); // same trace ID
  EXPECT_EQ(traceparent_value[35], '-');            // separator after trace-id
  EXPECT_EQ(traceparent_value[52], '-');            // separator after span-id
  EXPECT_EQ(traceparent_value.substr(53, 2), "01"); // sampled flag

  // Step 4: Test W3C extraction fallback when B3 headers are not present
  Tracing::TestTraceContextImpl w3c_only_context{
      {"traceparent", "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"}};

  Tracing::SpanPtr w3c_span =
      driver_->startSpan(config_, w3c_only_context, stream_info_, "w3c_test_operation",
                         {Tracing::Reason::Sampling, true});

  // Inject context for W3C extracted span
  Tracing::TestTraceContextImpl w3c_outgoing_context{{}};
  w3c_span->injectContext(w3c_outgoing_context, upstream_context);

  // Verify both B3 and W3C headers are injected even when extracted from W3C
  EXPECT_TRUE(w3c_outgoing_context.get("x-b3-traceid").has_value());
  EXPECT_TRUE(w3c_outgoing_context.get("x-b3-spanid").has_value());
  EXPECT_TRUE(w3c_outgoing_context.get("x-b3-sampled").has_value());
  EXPECT_TRUE(w3c_outgoing_context.get("traceparent").has_value());

  // Verify the trace ID is preserved from W3C extraction
  auto w3c_b3_traceid = w3c_outgoing_context.get("x-b3-traceid");
  EXPECT_EQ(w3c_b3_traceid.value(), "4bf92f3577b34da6a3ce929d0e0e4736");
}

TEST_F(ZipkinDriverTest, AllowCollectorClusterToBeAddedViaApi) {
  cm_.initializeClusters({"fake_cluster"}, {});
  ON_CALL(*cm_.active_clusters_["fake_cluster"]->info_, addedViaApi()).WillByDefault(Return(true));

  const std::string yaml_string = R"EOF(
  collector_cluster: fake_cluster
  collector_endpoint: /api/v2/spans
  collector_endpoint_version: HTTP_JSON
  )EOF";
  envoy::config::trace::v3::ZipkinConfig zipkin_config;
  TestUtility::loadFromYaml(yaml_string, zipkin_config);

  setup(zipkin_config, true);
}

TEST_F(ZipkinDriverTest, FlushSeveralSpansHttpJson) {
  expectValidFlushSeveralSpans("HTTP_JSON", "application/json");
}

TEST_F(ZipkinDriverTest, FlushSeveralSpansHttpJsonWithHostname) {
  expectValidFlushSeveralSpansWithHostname("HTTP_JSON", "application/json", "zipkin.fakedomain.io");
}

TEST_F(ZipkinDriverTest, FlushSeveralSpansHttpProto) {
  expectValidFlushSeveralSpans("HTTP_PROTO", "application/x-protobuf");
}

TEST_F(ZipkinDriverTest, FlushOneSpanReportFailure) {
  setupValidDriver("HTTP_JSON");

  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callback;
  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));

  EXPECT_CALL(cm_.thread_local_cluster_.async_client_,
              send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(timeout)))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            EXPECT_EQ("/api/v2/spans", message->headers().getPathValue());
            EXPECT_EQ("fake_cluster", message->headers().getHostValue());
            EXPECT_EQ("application/json", message->headers().getContentTypeValue());

            return &request;
          }));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.min_flush_spans", 5))
      .WillOnce(Return(1));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.request_timeout", 5000U))
      .WillOnce(Return(5000U));

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});
  span->finishSpan();

  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "404"}}}));

  // AsyncClient can fail with valid HTTP headers
  callback->onSuccess(request, std::move(msg));

  EXPECT_EQ(1U, stats_.counter("tracing.zipkin.spans_sent").value());
  EXPECT_EQ(0U, stats_.counter("tracing.zipkin.reports_skipped_no_cluster").value());
  EXPECT_EQ(0U, stats_.counter("tracing.zipkin.reports_sent").value());
  EXPECT_EQ(1U, stats_.counter("tracing.zipkin.reports_dropped").value());
  EXPECT_EQ(0U, stats_.counter("tracing.zipkin.reports_failed").value());
}

TEST_F(ZipkinDriverTest, SkipReportIfCollectorClusterHasBeenRemoved) {
  Upstream::ClusterUpdateCallbacks* cluster_update_callbacks;
  EXPECT_CALL(cm_, addThreadLocalClusterUpdateCallbacks_(_))
      .WillOnce(DoAll(SaveArgAddress(&cluster_update_callbacks), Return(nullptr)));

  setupValidDriver("HTTP_JSON");

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.min_flush_spans", 5))
      .WillRepeatedly(Return(1));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.request_timeout", 5000U))
      .WillRepeatedly(Return(5000U));

  // Verify the effect of onClusterAddOrUpdate()/onClusterRemoval() on reporting logic,
  // keeping in mind that they will be called both for relevant and irrelevant clusters.

  {
    // Simulate removal of the relevant cluster.
    cluster_update_callbacks->onClusterRemoval("fake_cluster");

    // Verify that no report will be sent.
    EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient()).Times(0);
    EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _)).Times(0);

    // Trigger flush of a span.
    driver_
        ->startSpan(config_, request_headers_, stream_info_, operation_name_,
                    {Tracing::Reason::Sampling, true})
        ->finishSpan();

    // Verify observability.
    EXPECT_EQ(1U, stats_.counter("tracing.zipkin.spans_sent").value());
    EXPECT_EQ(1U, stats_.counter("tracing.zipkin.reports_skipped_no_cluster").value());
    EXPECT_EQ(0U, stats_.counter("tracing.zipkin.reports_sent").value());
    EXPECT_EQ(0U, stats_.counter("tracing.zipkin.reports_dropped").value());
    EXPECT_EQ(0U, stats_.counter("tracing.zipkin.reports_failed").value());
  }

  {
    // Simulate addition of an irrelevant cluster.
    NiceMock<Upstream::MockThreadLocalCluster> unrelated_cluster;
    unrelated_cluster.cluster_.info_->name_ = "unrelated_cluster";
    Upstream::ThreadLocalClusterCommand command =
        [&unrelated_cluster]() -> Upstream::ThreadLocalCluster& { return unrelated_cluster; };
    cluster_update_callbacks->onClusterAddOrUpdate(unrelated_cluster.cluster_.info_->name_,
                                                   command);

    // Verify that no report will be sent.
    EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient()).Times(0);
    EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _)).Times(0);

    // Trigger flush of a span.
    driver_
        ->startSpan(config_, request_headers_, stream_info_, operation_name_,
                    {Tracing::Reason::Sampling, true})
        ->finishSpan();

    // Verify observability.
    EXPECT_EQ(2U, stats_.counter("tracing.zipkin.spans_sent").value());
    EXPECT_EQ(2U, stats_.counter("tracing.zipkin.reports_skipped_no_cluster").value());
    EXPECT_EQ(0U, stats_.counter("tracing.zipkin.reports_sent").value());
    EXPECT_EQ(0U, stats_.counter("tracing.zipkin.reports_dropped").value());
    EXPECT_EQ(0U, stats_.counter("tracing.zipkin.reports_failed").value());
  }

  {
    // Simulate addition of the relevant cluster.
    Upstream::ThreadLocalClusterCommand command = [this]() -> Upstream::ThreadLocalCluster& {
      return cm_.thread_local_cluster_;
    };
    cluster_update_callbacks->onClusterAddOrUpdate(cm_.thread_local_cluster_.info()->name(),
                                                   command);

    // Verify that report will be sent.
    EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient())
        .WillOnce(ReturnRef(cm_.thread_local_cluster_.async_client_));
    Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
    Http::AsyncClient::Callbacks* callback{};
    EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&callback)), Return(&request)));

    // Trigger flush of a span.
    driver_
        ->startSpan(config_, request_headers_, stream_info_, operation_name_,
                    {Tracing::Reason::Sampling, true})
        ->finishSpan();

    // Complete in-flight request.
    callback->onFailure(request, Http::AsyncClient::FailureReason::Reset);

    // Verify observability.
    EXPECT_EQ(3U, stats_.counter("tracing.zipkin.spans_sent").value());
    EXPECT_EQ(2U, stats_.counter("tracing.zipkin.reports_skipped_no_cluster").value());
    EXPECT_EQ(0U, stats_.counter("tracing.zipkin.reports_sent").value());
    EXPECT_EQ(0U, stats_.counter("tracing.zipkin.reports_dropped").value());
    EXPECT_EQ(1U, stats_.counter("tracing.zipkin.reports_failed").value());
  }

  {
    // Simulate removal of an irrelevant cluster.
    cluster_update_callbacks->onClusterRemoval("unrelated_cluster");

    // Verify that report will be sent.
    EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient())
        .WillOnce(ReturnRef(cm_.thread_local_cluster_.async_client_));
    Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
    Http::AsyncClient::Callbacks* callback{};
    EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&callback)), Return(&request)));

    // Trigger flush of a span.
    driver_
        ->startSpan(config_, request_headers_, stream_info_, operation_name_,
                    {Tracing::Reason::Sampling, true})
        ->finishSpan();

    // Complete in-flight request.
    Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
        Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "202"}}}));
    callback->onSuccess(request, std::move(msg));

    // Verify observability.
    EXPECT_EQ(4U, stats_.counter("tracing.zipkin.spans_sent").value());
    EXPECT_EQ(2U, stats_.counter("tracing.zipkin.reports_skipped_no_cluster").value());
    EXPECT_EQ(1U, stats_.counter("tracing.zipkin.reports_sent").value());
    EXPECT_EQ(0U, stats_.counter("tracing.zipkin.reports_dropped").value());
    EXPECT_EQ(1U, stats_.counter("tracing.zipkin.reports_failed").value());
  }
}

TEST_F(ZipkinDriverTest, CancelInflightRequestsOnDestruction) {
  setupValidDriver("HTTP_JSON");

  StrictMock<Http::MockAsyncClientRequest> request1(&cm_.thread_local_cluster_.async_client_),
      request2(&cm_.thread_local_cluster_.async_client_),
      request3(&cm_.thread_local_cluster_.async_client_),
      request4(&cm_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callback{};
  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));

  // Expect 4 separate report requests to be made.
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_,
              send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(timeout)))
      .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&callback)), Return(&request1)))
      .WillOnce(Return(&request2))
      .WillOnce(Return(&request3))
      .WillOnce(Return(&request4));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.min_flush_spans", 5))
      .Times(4)
      .WillRepeatedly(Return(1));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.request_timeout", 5000U))
      .Times(4)
      .WillRepeatedly(Return(5000U));

  // Trigger 1st report request.
  driver_
      ->startSpan(config_, request_headers_, stream_info_, operation_name_,
                  {Tracing::Reason::Sampling, true})
      ->finishSpan();
  // Trigger 2nd report request.
  driver_
      ->startSpan(config_, request_headers_, stream_info_, operation_name_,
                  {Tracing::Reason::Sampling, true})
      ->finishSpan();
  // Trigger 3rd report request.
  driver_
      ->startSpan(config_, request_headers_, stream_info_, operation_name_,
                  {Tracing::Reason::Sampling, true})
      ->finishSpan();
  // Trigger 4th report request.
  driver_
      ->startSpan(config_, request_headers_, stream_info_, operation_name_,
                  {Tracing::Reason::Sampling, true})
      ->finishSpan();

  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "404"}}}));

  // Simulate completion of the 2nd report request.
  callback->onSuccess(request2, std::move(msg));

  // Simulate failure of the 3rd report request.
  callback->onFailure(request3, Http::AsyncClient::FailureReason::Reset);

  // Expect 1st and 4th requests to be cancelled on destruction.
  EXPECT_CALL(request1, cancel());
  EXPECT_CALL(request4, cancel());

  // Trigger destruction.
  driver_.reset();
}

TEST_F(ZipkinDriverTest, FlushSpansTimer) {
  setupValidDriver("HTTP_JSON");

  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_,
              send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(timeout)));

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.min_flush_spans", 5))
      .WillOnce(Return(5));

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});
  span->finishSpan();

  // Timer should be re-enabled.
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5000), _));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.request_timeout", 5000U))
      .WillOnce(Return(5000U));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.flush_interval_ms", 5000U))
      .WillOnce(Return(5000U));

  timer_->invokeCallback();

  EXPECT_EQ(1U, stats_.counter("tracing.zipkin.timer_flushed").value());
  EXPECT_EQ(1U, stats_.counter("tracing.zipkin.spans_sent").value());
}

TEST_F(ZipkinDriverTest, NoB3ContextSampledTrue) {
  setupValidDriver("HTTP_JSON");

  EXPECT_TRUE(!request_headers_.get(ZipkinCoreConstants::get().X_B3_SPAN_ID.key()).has_value());
  EXPECT_TRUE(!request_headers_.get(ZipkinCoreConstants::get().X_B3_TRACE_ID.key()).has_value());
  EXPECT_TRUE(!request_headers_.get(ZipkinCoreConstants::get().X_B3_SAMPLED.key()).has_value());

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});

  Zipkin::SpanPtr zipkin_span(dynamic_cast<Zipkin::Span*>(span.release()));
  EXPECT_TRUE(zipkin_span->sampled());
}

TEST_F(ZipkinDriverTest, NoB3ContextSampledFalse) {
  setupValidDriver("HTTP_JSON");

  EXPECT_TRUE(!request_headers_.get(ZipkinCoreConstants::get().X_B3_SPAN_ID.key()).has_value());
  EXPECT_TRUE(!request_headers_.get(ZipkinCoreConstants::get().X_B3_TRACE_ID.key()).has_value());
  EXPECT_TRUE(!request_headers_.get(ZipkinCoreConstants::get().X_B3_SAMPLED.key()).has_value());

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, false});

  Zipkin::SpanPtr zipkin_span(dynamic_cast<Zipkin::Span*>(span.release()));
  EXPECT_FALSE(zipkin_span->sampled());
}

TEST_F(ZipkinDriverTest, PropagateB3NoSampleDecisionSampleTrue) {
  setupValidDriver("HTTP_JSON");

  request_headers_.set(ZipkinCoreConstants::get().X_B3_TRACE_ID.key(),
                       Hex::uint64ToHex(generateRandom64()));
  request_headers_.set(ZipkinCoreConstants::get().X_B3_SPAN_ID.key(),
                       Hex::uint64ToHex(generateRandom64()));
  EXPECT_TRUE(!request_headers_.get(ZipkinCoreConstants::get().X_B3_SAMPLED.key()).has_value());

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});

  Zipkin::SpanPtr zipkin_span(dynamic_cast<Zipkin::Span*>(span.release()));
  EXPECT_TRUE(zipkin_span->sampled());
}

TEST_F(ZipkinDriverTest, PropagateB3NoSampleDecisionSampleFalse) {
  setupValidDriver("HTTP_JSON");

  request_headers_.set(ZipkinCoreConstants::get().X_B3_TRACE_ID.key(),
                       Hex::uint64ToHex(generateRandom64()));
  request_headers_.set(ZipkinCoreConstants::get().X_B3_SPAN_ID.key(),
                       Hex::uint64ToHex(generateRandom64()));
  EXPECT_TRUE(!request_headers_.get(ZipkinCoreConstants::get().X_B3_SAMPLED.key()).has_value());

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, false});

  Zipkin::SpanPtr zipkin_span(dynamic_cast<Zipkin::Span*>(span.release()));
  EXPECT_FALSE(zipkin_span->sampled());
}

TEST_F(ZipkinDriverTest, PropagateB3NotSampled) {
  setupValidDriver("HTTP_JSON");

  EXPECT_TRUE(!request_headers_.get(ZipkinCoreConstants::get().X_B3_SPAN_ID.key()).has_value());
  EXPECT_TRUE(!request_headers_.get(ZipkinCoreConstants::get().X_B3_TRACE_ID.key()).has_value());

  // Only context header set is B3 sampled to indicate trace should not be sampled
  request_headers_.set(ZipkinCoreConstants::get().X_B3_SAMPLED.key(), NOT_SAMPLED);
  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});

  request_headers_.remove(ZipkinCoreConstants::get().X_B3_SAMPLED.key());

  span->injectContext(request_headers_, Tracing::UpstreamContext());

  auto sampled_entry = request_headers_.get(ZipkinCoreConstants::get().X_B3_SAMPLED.key());

  // Check B3 sampled flag is set to not sample
  EXPECT_EQ(NOT_SAMPLED, sampled_entry.value());
}

TEST_F(ZipkinDriverTest, PropagateB3NotSampledWithFalse) {
  setupValidDriver("HTTP_JSON");

  EXPECT_TRUE(!request_headers_.get(ZipkinCoreConstants::get().X_B3_SPAN_ID.key()).has_value());
  EXPECT_TRUE(!request_headers_.get(ZipkinCoreConstants::get().X_B3_TRACE_ID.key()).has_value());

  // Only context header set is B3 sampled to indicate trace should not be sampled (using legacy
  // 'false' value)
  const std::string sampled = "false";
  request_headers_.set(ZipkinCoreConstants::get().X_B3_SAMPLED.key(), sampled);
  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});

  request_headers_.remove(ZipkinCoreConstants::get().X_B3_SAMPLED.key());

  span->injectContext(request_headers_, Tracing::UpstreamContext());

  auto sampled_entry = request_headers_.get(ZipkinCoreConstants::get().X_B3_SAMPLED.key());
  // Check B3 sampled flag is set to not sample
  EXPECT_EQ(NOT_SAMPLED, sampled_entry.value());
}

TEST_F(ZipkinDriverTest, PropagateB3SampledWithTrue) {
  setupValidDriver("HTTP_JSON");

  EXPECT_TRUE(!request_headers_.get(ZipkinCoreConstants::get().X_B3_SPAN_ID.key()).has_value());
  EXPECT_TRUE(!request_headers_.get(ZipkinCoreConstants::get().X_B3_TRACE_ID.key()).has_value());

  // Only context header set is B3 sampled to indicate trace should be sampled (using legacy
  // 'true' value)
  const std::string sampled = "true";
  request_headers_.set(ZipkinCoreConstants::get().X_B3_SAMPLED.key(), sampled);
  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, false});

  request_headers_.remove(ZipkinCoreConstants::get().X_B3_SAMPLED.key());

  span->injectContext(request_headers_, Tracing::UpstreamContext());

  auto sampled_entry = request_headers_.get(ZipkinCoreConstants::get().X_B3_SAMPLED.key());
  // Check B3 sampled flag is set to sample
  EXPECT_EQ(SAMPLED, sampled_entry.value());
}

TEST_F(ZipkinDriverTest, PropagateB3SampleFalse) {
  setupValidDriver("HTTP_JSON");

  request_headers_.set(ZipkinCoreConstants::get().X_B3_TRACE_ID.key(),
                       Hex::uint64ToHex(generateRandom64()));
  request_headers_.set(ZipkinCoreConstants::get().X_B3_SPAN_ID.key(),
                       Hex::uint64ToHex(generateRandom64()));
  request_headers_.set(ZipkinCoreConstants::get().X_B3_SAMPLED.key(), NOT_SAMPLED);

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});

  Zipkin::SpanPtr zipkin_span(dynamic_cast<Zipkin::Span*>(span.release()));
  EXPECT_FALSE(zipkin_span->sampled());
}

TEST_F(ZipkinDriverTest, ZipkinSpanTest) {
  setupValidDriver("HTTP_JSON");

  // ====
  // Test effective setTag()
  // ====

  request_headers_.remove(Http::CustomHeaders::get().OtSpanContext);

  // New span will have a CS annotation
  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});

  Zipkin::SpanPtr zipkin_span(dynamic_cast<Zipkin::Span*>(span.release()));
  zipkin_span->setTag("key", "value");

  EXPECT_EQ(1ULL, zipkin_span->binaryAnnotations().size());
  EXPECT_EQ("key", zipkin_span->binaryAnnotations()[0].key());
  EXPECT_EQ("value", zipkin_span->binaryAnnotations()[0].value());

  // ====
  // Test setTag() with SR annotated span
  // ====

  const std::string trace_id = Hex::uint64ToHex(generateRandom64());
  const std::string span_id = Hex::uint64ToHex(generateRandom64());
  const std::string parent_id = Hex::uint64ToHex(generateRandom64());
  const std::string context = trace_id + ";" + span_id + ";" + parent_id + ";" + CLIENT_SEND;

  request_headers_.set(Http::CustomHeaders::get().OtSpanContext, context);

  // New span will have an SR annotation
  Tracing::SpanPtr span2 = driver_->startSpan(config_, request_headers_, stream_info_,
                                              operation_name_, {Tracing::Reason::Sampling, true});

  Zipkin::SpanPtr zipkin_span2(dynamic_cast<Zipkin::Span*>(span2.release()));
  zipkin_span2->setTag("key2", "value2");

  EXPECT_EQ(1ULL, zipkin_span2->binaryAnnotations().size());
  EXPECT_EQ("key2", zipkin_span2->binaryAnnotations()[0].key());
  EXPECT_EQ("value2", zipkin_span2->binaryAnnotations()[0].value());

  // ====
  // Test setTag() with empty annotations vector
  // ====
  Tracing::SpanPtr span3 = driver_->startSpan(config_, request_headers_, stream_info_,
                                              operation_name_, {Tracing::Reason::Sampling, true});
  Zipkin::SpanPtr zipkin_span3(dynamic_cast<Zipkin::Span*>(span3.release()));

  std::vector<Annotation> annotations;
  zipkin_span3->setAnnotations(annotations);

  zipkin_span3->setTag("key3", "value3");
  EXPECT_EQ(1ULL, zipkin_span3->binaryAnnotations().size());
  EXPECT_EQ("key3", zipkin_span3->binaryAnnotations()[0].key());
  EXPECT_EQ("value3", zipkin_span3->binaryAnnotations()[0].value());

  // ====
  // Test effective log()
  // ====

  Tracing::SpanPtr span4 = driver_->startSpan(config_, request_headers_, stream_info_,
                                              operation_name_, {Tracing::Reason::Sampling, true});
  const auto timestamp =
      SystemTime{std::chrono::duration_cast<SystemTime::duration>(std::chrono::hours{123})};
  const auto timestamp_count =
      std::chrono::duration_cast<std::chrono::microseconds>(timestamp.time_since_epoch()).count();
  span4->log(timestamp, "abc");

  Zipkin::SpanPtr zipkin_span4(dynamic_cast<Zipkin::Span*>(span4.release()));
  EXPECT_FALSE(zipkin_span4->annotations().empty());
  EXPECT_EQ(timestamp_count, zipkin_span4->annotations().back().timestamp());
  EXPECT_EQ("abc", zipkin_span4->annotations().back().value());

  // ====
  // Test baggage noop
  // ====
  Tracing::SpanPtr span5 = driver_->startSpan(config_, request_headers_, stream_info_,
                                              operation_name_, {Tracing::Reason::Sampling, true});
  span5->setBaggage("baggage_key", "baggage_value");
  EXPECT_EQ("", span5->getBaggage("baggage_key"));

  // ====
  // Test trace id noop
  // ====
  Tracing::SpanPtr span6 = driver_->startSpan(config_, request_headers_, stream_info_,
                                              operation_name_, {Tracing::Reason::Sampling, true});
  EXPECT_EQ(span6->getTraceId(), "0000000000000000");
  EXPECT_EQ(span6->getSpanId(), "");
}

TEST_F(ZipkinDriverTest, ZipkinSpanContextFromB3HeadersTest) {
  setupValidDriver("HTTP_JSON");

  const std::string trace_id = Hex::uint64ToHex(generateRandom64());
  const std::string span_id = Hex::uint64ToHex(generateRandom64());
  const std::string parent_id = Hex::uint64ToHex(generateRandom64());

  request_headers_.set(ZipkinCoreConstants::get().X_B3_TRACE_ID.key(), trace_id);
  request_headers_.set(ZipkinCoreConstants::get().X_B3_SPAN_ID.key(), span_id);
  request_headers_.set(ZipkinCoreConstants::get().X_B3_PARENT_SPAN_ID.key(), parent_id);

  // New span will have an SR annotation - so its span and parent ids will be
  // the same as the supplied span context (i.e. shared context)
  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});

  Zipkin::SpanPtr zipkin_span(dynamic_cast<Zipkin::Span*>(span.release()));

  EXPECT_EQ(trace_id, zipkin_span->traceIdAsHexString());
  EXPECT_EQ(span_id, zipkin_span->idAsHexString());
  EXPECT_EQ(parent_id, zipkin_span->parentIdAsHexString());
  EXPECT_TRUE(zipkin_span->sampled());
}

TEST_F(ZipkinDriverTest, ZipkinSpanContextFromB3HeadersEmptyParentSpanTest) {
  setupValidDriver("HTTP_JSON");

  // Root span so have same trace and span id
  const std::string id = Hex::uint64ToHex(generateRandom64());
  request_headers_.set(ZipkinCoreConstants::get().X_B3_TRACE_ID.key(), id);
  request_headers_.set(ZipkinCoreConstants::get().X_B3_SPAN_ID.key(), id);
  request_headers_.set(ZipkinCoreConstants::get().X_B3_SAMPLED.key(), SAMPLED);

  // Set parent span id to empty string, to ensure it is ignored
  const std::string parent_span_id = "";
  request_headers_.set(ZipkinCoreConstants::get().X_B3_PARENT_SPAN_ID.key(), parent_span_id);

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});

  Zipkin::SpanPtr zipkin_span(dynamic_cast<Zipkin::Span*>(span.release()));
  EXPECT_TRUE(zipkin_span->sampled());
}

TEST_F(ZipkinDriverTest, ZipkinSpanContextFromB3Headers128TraceIdTest) {
  setupValidDriver("HTTP_JSON");

  const uint64_t trace_id_high = generateRandom64();
  const uint64_t trace_id_low = generateRandom64();
  const std::string trace_id = Hex::uint64ToHex(trace_id_high) + Hex::uint64ToHex(trace_id_low);
  const std::string span_id = Hex::uint64ToHex(generateRandom64());
  const std::string parent_id = Hex::uint64ToHex(generateRandom64());

  request_headers_.set(ZipkinCoreConstants::get().X_B3_TRACE_ID.key(), trace_id);
  request_headers_.set(ZipkinCoreConstants::get().X_B3_SPAN_ID.key(), span_id);
  request_headers_.set(ZipkinCoreConstants::get().X_B3_PARENT_SPAN_ID.key(), parent_id);

  // New span will have an SR annotation - so its span and parent ids will be
  // the same as the supplied span context (i.e. shared context)
  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});

  Zipkin::SpanPtr zipkin_span(dynamic_cast<Zipkin::Span*>(span.release()));

  EXPECT_EQ(trace_id_high, zipkin_span->traceIdHigh());
  EXPECT_EQ(trace_id_low, zipkin_span->traceId());
  EXPECT_EQ(trace_id, zipkin_span->traceIdAsHexString());
  EXPECT_EQ(span_id, zipkin_span->idAsHexString());
  EXPECT_EQ(parent_id, zipkin_span->parentIdAsHexString());
  EXPECT_TRUE(zipkin_span->sampled());
  EXPECT_EQ(trace_id, zipkin_span->getTraceId());
  EXPECT_EQ("", zipkin_span->getSpanId());
}

TEST_F(ZipkinDriverTest, ZipkinSpanContextFromInvalidTraceIdB3HeadersTest) {
  setupValidDriver("HTTP_JSON");

  request_headers_.set(ZipkinCoreConstants::get().X_B3_TRACE_ID.key(), std::string("xyz"));
  request_headers_.set(ZipkinCoreConstants::get().X_B3_SPAN_ID.key(),
                       Hex::uint64ToHex(generateRandom64()));
  request_headers_.set(ZipkinCoreConstants::get().X_B3_PARENT_SPAN_ID.key(),
                       Hex::uint64ToHex(generateRandom64()));

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});
  EXPECT_NE(nullptr, dynamic_cast<Tracing::NullSpan*>(span.get()));
}

TEST_F(ZipkinDriverTest, ZipkinSpanContextFromInvalidSpanIdB3HeadersTest) {
  setupValidDriver("HTTP_JSON");

  request_headers_.set(ZipkinCoreConstants::get().X_B3_TRACE_ID.key(),
                       Hex::uint64ToHex(generateRandom64()));
  request_headers_.set(ZipkinCoreConstants::get().X_B3_SPAN_ID.key(), std::string("xyz"));
  request_headers_.set(ZipkinCoreConstants::get().X_B3_PARENT_SPAN_ID.key(),
                       Hex::uint64ToHex(generateRandom64()));

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});
  EXPECT_NE(nullptr, dynamic_cast<Tracing::NullSpan*>(span.get()));
}

TEST_F(ZipkinDriverTest, ZipkinSpanContextFromInvalidParentIdB3HeadersTest) {
  setupValidDriver("HTTP_JSON");

  request_headers_.set(ZipkinCoreConstants::get().X_B3_TRACE_ID.key(),
                       Hex::uint64ToHex(generateRandom64()));
  request_headers_.set(ZipkinCoreConstants::get().X_B3_SPAN_ID.key(),
                       Hex::uint64ToHex(generateRandom64()));
  request_headers_.set(ZipkinCoreConstants::get().X_B3_PARENT_SPAN_ID.key(), std::string("xyz"));

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});
  EXPECT_NE(nullptr, dynamic_cast<Tracing::NullSpan*>(span.get()));
}

TEST_F(ZipkinDriverTest, ExplicitlySetSampledFalse) {
  setupValidDriver("HTTP_JSON");

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});

  span->setSampled(false);

  request_headers_.remove(ZipkinCoreConstants::get().X_B3_SAMPLED.key());

  span->injectContext(request_headers_, Tracing::UpstreamContext());

  auto sampled_entry = request_headers_.get(ZipkinCoreConstants::get().X_B3_SAMPLED.key());
  // Check B3 sampled flag is set to not sample
  EXPECT_EQ(NOT_SAMPLED, sampled_entry.value());
}

TEST_F(ZipkinDriverTest, ExplicitlySetSampledTrue) {
  setupValidDriver("HTTP_JSON");

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, false});

  span->setSampled(true);

  request_headers_.remove(ZipkinCoreConstants::get().X_B3_SAMPLED.key());

  span->injectContext(request_headers_, Tracing::UpstreamContext());

  auto sampled_entry = request_headers_.get(ZipkinCoreConstants::get().X_B3_SAMPLED.key());
  // Check B3 sampled flag is set to sample
  EXPECT_EQ(SAMPLED, sampled_entry.value());
}

TEST_F(ZipkinDriverTest, UseLocalDecisionTrue) {
  setupValidDriver("HTTP_JSON");

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});
  EXPECT_TRUE(span->useLocalDecision());

  request_headers_.remove(ZipkinCoreConstants::get().X_B3_SAMPLED.key());

  span->injectContext(request_headers_, Tracing::UpstreamContext());

  auto sampled_entry = request_headers_.get(ZipkinCoreConstants::get().X_B3_SAMPLED.key());
  EXPECT_EQ(SAMPLED, sampled_entry.value());
}

TEST_F(ZipkinDriverTest, UseLocalDecisionFalse) {
  setupValidDriver("HTTP_JSON");
  request_headers_.set(ZipkinCoreConstants::get().X_B3_SAMPLED.key(), NOT_SAMPLED);

  // Envoy tracing decision is ignored if the B3 sampled header is set to not sample.
  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});
  EXPECT_FALSE(span->useLocalDecision());

  request_headers_.remove(ZipkinCoreConstants::get().X_B3_SAMPLED.key());

  span->injectContext(request_headers_, Tracing::UpstreamContext());

  auto sampled_entry = request_headers_.get(ZipkinCoreConstants::get().X_B3_SAMPLED.key());
  EXPECT_EQ(NOT_SAMPLED, sampled_entry.value());
}

TEST_F(ZipkinDriverTest, DuplicatedHeader) {
  setupValidDriver("HTTP_JSON");
  request_headers_.set(ZipkinCoreConstants::get().X_B3_TRACE_ID.key(),
                       Hex::uint64ToHex(generateRandom64()));
  request_headers_.set(ZipkinCoreConstants::get().X_B3_SPAN_ID.key(),
                       Hex::uint64ToHex(generateRandom64()));
  request_headers_.set(ZipkinCoreConstants::get().X_B3_PARENT_SPAN_ID.key(),
                       Hex::uint64ToHex(generateRandom64()));
  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, false});

  using DupCallback = std::function<bool(absl::string_view key)>;
  DupCallback dup_callback = [](absl::string_view key) -> bool {
    static absl::flat_hash_map<std::string, bool> dup;
    if (dup.find(key) == dup.end()) {
      dup[key] = true;
      return false;
    }
    return true;
  };

  span->setSampled(true);
  span->injectContext(request_headers_, Tracing::UpstreamContext());
  request_headers_.forEach([&dup_callback](absl::string_view key, absl::string_view) -> bool {
    dup_callback(key);
    return true;
  });
}

TEST_F(ZipkinDriverTest, ReporterFlushWithHttpServiceHeadersVerifyHeaders) {
  cm_.initializeClusters({"fake_cluster", "legacy_cluster"}, {});

  const std::string yaml_string = R"EOF(
  collector_cluster: legacy_cluster
  collector_endpoint: /legacy/api/v1/spans
  collector_service:
    http_uri:
      uri: "https://zipkin-collector.example.com/api/v2/spans"
      cluster: fake_cluster
      timeout: 5s
    request_headers_to_add:
      - header:
          key: "Authorization"
          value: "Bearer token123"
      - header:
          key: "X-Custom-Header"
          value: "custom-value"
      - header:
          key: "X-API-Key"
          value: "api-key-123"
  collector_endpoint_version: HTTP_JSON
  )EOF";

  envoy::config::trace::v3::ZipkinConfig zipkin_config;
  TestUtility::loadFromYaml(yaml_string, zipkin_config);
  setup(zipkin_config, true);

  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callback;
  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));

  // Set up expectations for the HTTP request with custom headers
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_,
              send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(timeout)))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            // Verify standard headers are present
            EXPECT_EQ("/api/v2/spans", message->headers().getPathValue());
            EXPECT_EQ("zipkin-collector.example.com", message->headers().getHostValue());
            EXPECT_EQ("application/json", message->headers().getContentTypeValue());

            // Verify custom headers are present
            auto auth_header = message->headers().get(Http::LowerCaseString("authorization"));
            EXPECT_FALSE(auth_header.empty());
            EXPECT_EQ("Bearer token123", auth_header[0]->value().getStringView());

            auto custom_header = message->headers().get(Http::LowerCaseString("x-custom-header"));
            EXPECT_FALSE(custom_header.empty());
            EXPECT_EQ("custom-value", custom_header[0]->value().getStringView());

            auto api_key_header = message->headers().get(Http::LowerCaseString("x-api-key"));
            EXPECT_FALSE(api_key_header.empty());
            EXPECT_EQ("api-key-123", api_key_header[0]->value().getStringView());

            return &request;
          }));

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.min_flush_spans", 5))
      .WillOnce(Return(1));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.request_timeout", 5000U))
      .WillOnce(Return(5000U));

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});
  span->finishSpan();

  Http::ResponseHeaderMapPtr response_headers{
      new Http::TestResponseHeaderMapImpl{{":status", "202"}}};
  callback->onSuccess(request,
                      std::make_unique<Http::ResponseMessageImpl>(std::move(response_headers)));
}

// Test URI parsing edge cases to improve coverage
TEST_F(ZipkinDriverTest, DriverWithHttpServiceUriParsing) {
  cm_.initializeClusters({"fake_cluster"}, {});

  // Test 1: URI without hostname (should fallback to cluster name)
  const std::string yaml_string_no_host = R"EOF(
  collector_service:
    http_uri:
      uri: "/api/v2/spans"
      cluster: fake_cluster
      timeout: 5s
  collector_endpoint_version: HTTP_JSON
  )EOF";

  envoy::config::trace::v3::ZipkinConfig zipkin_config_no_host;
  TestUtility::loadFromYaml(yaml_string_no_host, zipkin_config_no_host);
  setup(zipkin_config_no_host, false);
  EXPECT_EQ("fake_cluster", driver_->hostnameForTest()); // Should fallback to cluster name
}

TEST_F(ZipkinDriverTest, DriverWithHttpServiceUriParsingNoPath) {
  cm_.initializeClusters({"fake_cluster"}, {});

  // Test 2: URI with hostname but no path (should use "/" as default)
  const std::string yaml_string_no_path = R"EOF(
  collector_service:
    http_uri:
      uri: "https://zipkin-collector.example.com"
      cluster: fake_cluster
      timeout: 5s
  collector_endpoint_version: HTTP_JSON
  )EOF";

  envoy::config::trace::v3::ZipkinConfig zipkin_config_no_path;
  TestUtility::loadFromYaml(yaml_string_no_path, zipkin_config_no_path);
  setup(zipkin_config_no_path, false);
  EXPECT_EQ("zipkin-collector.example.com", driver_->hostnameForTest());
}

TEST_F(ZipkinDriverTest, DriverWithHttpServiceUriParsingWithPort) {
  cm_.initializeClusters({"fake_cluster"}, {});

  // Test 3: URI with hostname and port
  const std::string yaml_string_with_port = R"EOF(
  collector_service:
    http_uri:
      uri: "http://zipkin-collector.example.com:9411/api/v2/spans"
      cluster: fake_cluster
      timeout: 5s
  collector_endpoint_version: HTTP_JSON
  )EOF";

  envoy::config::trace::v3::ZipkinConfig zipkin_config_with_port;
  TestUtility::loadFromYaml(yaml_string_with_port, zipkin_config_with_port);
  setup(zipkin_config_with_port, false);
  EXPECT_EQ("zipkin-collector.example.com:9411", driver_->hostnameForTest());
}

TEST_F(ZipkinDriverTest, DriverMissingCollectorConfiguration) {
  cm_.initializeClusters({"fake_cluster"}, {});

  // Test missing both collector_cluster and collector_service
  const std::string yaml_string_missing = R"EOF(
  collector_endpoint_version: HTTP_JSON
  )EOF";

  envoy::config::trace::v3::ZipkinConfig zipkin_config_missing;
  TestUtility::loadFromYaml(yaml_string_missing, zipkin_config_missing);

  EXPECT_THROW_WITH_MESSAGE(setup(zipkin_config_missing, false), EnvoyException,
                            "collector_cluster and collector_endpoint must be specified when not "
                            "using collector_service");
}

} // namespace
} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
