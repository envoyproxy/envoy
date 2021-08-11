#include <chrono>
#include <memory>
#include <sstream>
#include <string>

#include "envoy/config/trace/v3/lightstep.pb.h"

#include "source/common/common/base64.h"
#include "source/common/grpc/common.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/runtime/runtime_impl.h"
#include "source/common/stats/symbol_table_impl.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/extensions/tracers/lightstep/lightstep_tracer_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/thread_local_cluster.h"
#include "test/test_common/global.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AtLeast;
using testing::DoAll;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::WithArg;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Lightstep {

static Http::ResponseMessagePtr makeSuccessResponse() {
  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));

  msg->trailers(
      Http::ResponseTrailerMapPtr{new Http::TestResponseTrailerMapImpl{{"grpc-status", "0"}}});
  std::unique_ptr<Protobuf::Message> collector_response =
      lightstep::Transporter::MakeCollectorResponse();
  EXPECT_NE(collector_response, nullptr);
  msg->body().add(*Grpc::Common::serializeToGrpcFrame(*collector_response));
  return msg;
}

namespace {

class LightStepDriverTest : public testing::Test {
public:
  LightStepDriverTest() : grpc_context_(*symbol_table_) {}

  void setup(envoy::config::trace::v3::LightstepConfig& lightstep_config, bool init_timer,
             Common::Ot::OpenTracingDriver::PropagationMode propagation_mode =
                 Common::Ot::OpenTracingDriver::PropagationMode::TracerNative) {
    std::unique_ptr<lightstep::LightStepTracerOptions> opts(
        new lightstep::LightStepTracerOptions());
    opts->access_token = "sample_token";
    opts->component_name = "component";

    cm_.thread_local_cluster_.cluster_.info_->name_ = "fake_cluster";
    cm_.initializeThreadLocalClusters({"fake_cluster"});
    ON_CALL(cm_.thread_local_cluster_, httpAsyncClient())
        .WillByDefault(ReturnRef(cm_.thread_local_cluster_.async_client_));

    if (init_timer) {
      timer_ = new NiceMock<Event::MockTimer>(&tls_.dispatcher_);
      EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(1000), _)).Times(AtLeast(1));
    }

    driver_ = std::make_unique<LightStepDriver>(lightstep_config, cm_, stats_, tls_, runtime_,
                                                std::move(opts), propagation_mode, grpc_context_);
  }

  void setupValidDriver(int min_flush_spans = LightStepDriver::DefaultMinFlushSpans,
                        Common::Ot::OpenTracingDriver::PropagationMode propagation_mode =
                            Common::Ot::OpenTracingDriver::PropagationMode::TracerNative) {
    cm_.initializeClusters({"fake_cluster"}, {});
    ON_CALL(*cm_.active_clusters_["fake_cluster"]->info_, features())
        .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));

    EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.flush_interval_ms", _))
        .Times(AtLeast(1))
        .WillRepeatedly(Return(1000));

    EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.min_flush_spans",
                                               LightStepDriver::DefaultMinFlushSpans))
        .Times(AtLeast(1))
        .WillRepeatedly(Return(min_flush_spans));

    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    )EOF";
    envoy::config::trace::v3::LightstepConfig lightstep_config;
    TestUtility::loadFromYaml(yaml_string, lightstep_config);

    setup(lightstep_config, true, propagation_mode);
  }

  const std::string operation_name_{"test"};
  Http::TestRequestHeaderMapImpl request_headers_{
      {":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}};
  const Http::TestResponseHeaderMapImpl response_headers_{{":status", "500"}};
  SystemTime start_time_;
  StreamInfo::MockStreamInfo stream_info_;

  Stats::TestUtil::TestSymbolTable symbol_table_;
  Grpc::ContextImpl grpc_context_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_;
  std::unique_ptr<LightStepDriver> driver_;
  NiceMock<Event::MockTimer>* timer_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;

  NiceMock<Tracing::MockConfig> config_;
};

TEST_F(LightStepDriverTest, LightStepLogger) {
  LightStepLogger logger;

  // Verify calls to logger don't crash.
  logger(lightstep::LogLevel::debug, "abc");
  logger(lightstep::LogLevel::info, "abc");
  logger(lightstep::LogLevel::error, "abc");
}

TEST_F(LightStepDriverTest, InitializeDriver) {
  {
    envoy::config::trace::v3::LightstepConfig lightstep_config;

    EXPECT_THROW(setup(lightstep_config, false), EnvoyException);
  }

  {
    // Valid config but not valid cluster.
    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    )EOF";
    envoy::config::trace::v3::LightstepConfig lightstep_config;
    TestUtility::loadFromYaml(yaml_string, lightstep_config);

    EXPECT_THROW(setup(lightstep_config, false), EnvoyException);
  }

  {
    // Valid config, but upstream cluster does not support http2.
    cm_.initializeClusters({"fake_cluster"}, {});
    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    )EOF";
    envoy::config::trace::v3::LightstepConfig lightstep_config;
    TestUtility::loadFromYaml(yaml_string, lightstep_config);

    EXPECT_THROW(setup(lightstep_config, false), EnvoyException);
  }

  {
    ON_CALL(*cm_.active_clusters_["fake_cluster"]->info_, features())
        .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));
    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    )EOF";
    envoy::config::trace::v3::LightstepConfig lightstep_config;
    TestUtility::loadFromYaml(yaml_string, lightstep_config);

    setup(lightstep_config, true);
  }
}

TEST_F(LightStepDriverTest, DeferredTlsInitialization) {
  const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    )EOF";
  envoy::config::trace::v3::LightstepConfig lightstep_config;
  TestUtility::loadFromYaml(yaml_string, lightstep_config);

  std::unique_ptr<lightstep::LightStepTracerOptions> opts(new lightstep::LightStepTracerOptions());
  opts->access_token = "sample_token";
  opts->component_name = "component";

  ON_CALL(cm_.thread_local_cluster_, httpAsyncClient())
      .WillByDefault(ReturnRef(cm_.thread_local_cluster_.async_client_));

  auto propagation_mode = Common::Ot::OpenTracingDriver::PropagationMode::TracerNative;

  tls_.defer_data_ = true;
  cm_.initializeClusters({"fake_cluster"}, {});
  ON_CALL(*cm_.active_clusters_["fake_cluster"]->info_, features())
      .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));
  driver_ = std::make_unique<LightStepDriver>(lightstep_config, cm_, stats_, tls_, runtime_,
                                              std::move(opts), propagation_mode, grpc_context_);
  tls_.call();
}

TEST_F(LightStepDriverTest, AllowCollectorClusterToBeAddedViaApi) {
  cm_.initializeClusters({"fake_cluster"}, {});
  ON_CALL(*cm_.active_clusters_["fake_cluster"]->info_, features())
      .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));
  ON_CALL(*cm_.active_clusters_["fake_cluster"]->info_, addedViaApi()).WillByDefault(Return(true));

  const std::string yaml_string = R"EOF(
  collector_cluster: fake_cluster
  )EOF";
  envoy::config::trace::v3::LightstepConfig lightstep_config;
  TestUtility::loadFromYaml(yaml_string, lightstep_config);

  setup(lightstep_config, true);
}

TEST_F(LightStepDriverTest, FlushSeveralSpans) {
  setupValidDriver(2);

  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callback = nullptr;
  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));

  EXPECT_CALL(cm_.thread_local_cluster_.async_client_,
              send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(timeout)))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            EXPECT_EQ("/lightstep.collector.CollectorService/Report",
                      message->headers().getPathValue());
            EXPECT_EQ("fake_cluster", message->headers().getHostValue());
            EXPECT_EQ("application/grpc", message->headers().getContentTypeValue());

            return &request;
          }));

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.request_timeout", 5000U))
      .WillOnce(Return(5000U));

  Tracing::SpanPtr first_span = driver_->startSpan(config_, request_headers_, operation_name_,
                                                   start_time_, {Tracing::Reason::Sampling, true});

  // Currently not possible to access the operation from the span, but this
  // invocation will make sure setting the operation does not cause a crash!
  first_span->setOperation("myOperation");
  first_span->finishSpan();

  Tracing::SpanPtr second_span = driver_->startSpan(config_, request_headers_, operation_name_,
                                                    start_time_, {Tracing::Reason::Sampling, true});
  second_span->finishSpan();

  Tracing::SpanPtr third_span = driver_->startSpan(config_, request_headers_, operation_name_,
                                                   start_time_, {Tracing::Reason::Sampling, true});
  third_span->finishSpan();

  callback->onSuccess(request, makeSuccessResponse());

  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("grpc.lightstep.collector.CollectorService.Report.success")
                    .value());

  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("grpc.lightstep.collector.CollectorService.Report.total")
                    .value());
  EXPECT_EQ(2U, stats_.counter("tracing.lightstep.spans_sent").value());
  EXPECT_EQ(0U, stats_.counter("tracing.lightstep.reports_skipped_no_cluster").value());
}

TEST_F(LightStepDriverTest, SkipReportIfCollectorClusterHasBeenRemoved) {
  Upstream::ClusterUpdateCallbacks* cluster_update_callbacks;
  EXPECT_CALL(cm_, addThreadLocalClusterUpdateCallbacks_(_))
      .WillOnce(DoAll(SaveArgAddress(&cluster_update_callbacks), Return(nullptr)));

  setupValidDriver(1);

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.request_timeout", 5000U))
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
        ->startSpan(config_, request_headers_, operation_name_, start_time_,
                    {Tracing::Reason::Sampling, true})
        ->finishSpan();
    driver_->flush();

    // Verify observability.
    EXPECT_EQ(1U, stats_.counter("tracing.lightstep.reports_skipped_no_cluster").value());
    EXPECT_EQ(0U, stats_.counter("tracing.lightstep.spans_sent").value());
    EXPECT_EQ(0U, stats_.counter("tracing.lightstep.spans_dropped").value());
  }

  {
    // Simulate addition of an irrelevant cluster.
    NiceMock<Upstream::MockThreadLocalCluster> unrelated_cluster;
    unrelated_cluster.cluster_.info_->name_ = "unrelated_cluster";
    cluster_update_callbacks->onClusterAddOrUpdate(unrelated_cluster);

    // Verify that no report will be sent.
    EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient()).Times(0);
    EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _)).Times(0);

    // Trigger flush of a span.
    driver_
        ->startSpan(config_, request_headers_, operation_name_, start_time_,
                    {Tracing::Reason::Sampling, true})
        ->finishSpan();
    driver_->flush();

    // Verify observability.
    EXPECT_EQ(2U, stats_.counter("tracing.lightstep.reports_skipped_no_cluster").value());
    EXPECT_EQ(0U, stats_.counter("tracing.lightstep.spans_sent").value());
    EXPECT_EQ(0U, stats_.counter("tracing.lightstep.spans_dropped").value());
  }

  {
    // Simulate addition of the relevant cluster.
    cluster_update_callbacks->onClusterAddOrUpdate(cm_.thread_local_cluster_);

    // Verify that report will be sent.
    EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient())
        .WillOnce(ReturnRef(cm_.thread_local_cluster_.async_client_));
    Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
    Http::AsyncClient::Callbacks* callback{};
    EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&callback)), Return(&request)));

    // Trigger flush of a span.
    driver_
        ->startSpan(config_, request_headers_, operation_name_, start_time_,
                    {Tracing::Reason::Sampling, true})
        ->finishSpan();
    driver_->flush();

    // Complete in-flight request.
    callback->onFailure(request, Http::AsyncClient::FailureReason::Reset);

    // Verify observability.
    EXPECT_EQ(2U, stats_.counter("tracing.lightstep.reports_skipped_no_cluster").value());
    EXPECT_EQ(0U, stats_.counter("tracing.lightstep.spans_sent").value());
    EXPECT_EQ(1U, stats_.counter("tracing.lightstep.spans_dropped").value());
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
        ->startSpan(config_, request_headers_, operation_name_, start_time_,
                    {Tracing::Reason::Sampling, true})
        ->finishSpan();
    driver_->flush();

    // Complete in-flight request.
    Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
        Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
    callback->onSuccess(request, std::move(msg));

    // Verify observability.
    EXPECT_EQ(2U, stats_.counter("tracing.lightstep.reports_skipped_no_cluster").value());
    EXPECT_EQ(1U, stats_.counter("tracing.lightstep.spans_sent").value());
    EXPECT_EQ(1U, stats_.counter("tracing.lightstep.spans_dropped").value());
  }
}

TEST_F(LightStepDriverTest, FlushOneFailure) {
  setupValidDriver(1);

  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callback = nullptr;
  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));

  EXPECT_CALL(cm_.thread_local_cluster_.async_client_,
              send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(timeout)))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            EXPECT_EQ("/lightstep.collector.CollectorService/Report",
                      message->headers().getPathValue());
            EXPECT_EQ("fake_cluster", message->headers().getHostValue());
            EXPECT_EQ("application/grpc", message->headers().getContentTypeValue());

            return &request;
          }));

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.request_timeout", 5000U))
      .WillOnce(Return(5000U));

  Tracing::SpanPtr first_span = driver_->startSpan(config_, request_headers_, operation_name_,
                                                   start_time_, {Tracing::Reason::Sampling, true});

  first_span->finishSpan();

  Tracing::SpanPtr second_span = driver_->startSpan(config_, request_headers_, operation_name_,
                                                    start_time_, {Tracing::Reason::Sampling, true});

  second_span->finishSpan();

  callback->onFailure(request, Http::AsyncClient::FailureReason::Reset);

  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("grpc.lightstep.collector.CollectorService.Report.failure")
                    .value());
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("grpc.lightstep.collector.CollectorService.Report.total")
                    .value());
  EXPECT_EQ(1U, stats_.counter("tracing.lightstep.spans_dropped").value());
  EXPECT_EQ(0U, stats_.counter("tracing.lightstep.reports_skipped_no_cluster").value());
}

TEST_F(LightStepDriverTest, FlushWithActiveReport) {
  setupValidDriver(1);

  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callback = nullptr;
  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));

  EXPECT_CALL(cm_.thread_local_cluster_.async_client_,
              send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(timeout)))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            EXPECT_EQ("/lightstep.collector.CollectorService/Report",
                      message->headers().getPathValue());
            EXPECT_EQ("fake_cluster", message->headers().getHostValue());
            EXPECT_EQ("application/grpc", message->headers().getContentTypeValue());

            return &request;
          }));

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.request_timeout", 5000U))
      .WillOnce(Return(5000U));

  driver_
      ->startSpan(config_, request_headers_, operation_name_, start_time_,
                  {Tracing::Reason::Sampling, true})
      ->finishSpan();
  driver_->flush();

  driver_
      ->startSpan(config_, request_headers_, operation_name_, start_time_,
                  {Tracing::Reason::Sampling, true})
      ->finishSpan();
  driver_->flush();

  EXPECT_EQ(1U, stats_.counter("tracing.lightstep.spans_dropped").value());
  EXPECT_EQ(0U, stats_.counter("tracing.lightstep.reports_skipped_no_cluster").value());

  EXPECT_CALL(request, cancel());

  driver_.reset();
}

TEST_F(LightStepDriverTest, OnFullWithActiveReport) {
  setupValidDriver(1);

  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callback = nullptr;
  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));

  EXPECT_CALL(cm_.thread_local_cluster_.async_client_,
              send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(timeout)))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            EXPECT_EQ("/lightstep.collector.CollectorService/Report",
                      message->headers().getPathValue());
            EXPECT_EQ("fake_cluster", message->headers().getHostValue());
            EXPECT_EQ("application/grpc", message->headers().getContentTypeValue());

            return &request;
          }));

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.request_timeout", 5000U))
      .WillOnce(Return(5000U));

  driver_
      ->startSpan(config_, request_headers_, operation_name_, start_time_,
                  {Tracing::Reason::Sampling, true})
      ->finishSpan();
  driver_->flush();

  driver_
      ->startSpan(config_, request_headers_, operation_name_, start_time_,
                  {Tracing::Reason::Sampling, true})
      ->finishSpan();
  driver_
      ->startSpan(config_, request_headers_, operation_name_, start_time_,
                  {Tracing::Reason::Sampling, true})
      ->finishSpan();

  EXPECT_EQ(1U, stats_.counter("tracing.lightstep.spans_dropped").value());
  EXPECT_EQ(0U, stats_.counter("tracing.lightstep.reports_skipped_no_cluster").value());

  EXPECT_CALL(request, cancel());

  driver_.reset();
}

TEST_F(LightStepDriverTest, FlushSpansTimer) {
  setupValidDriver();

  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callback = nullptr;

  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));
  EXPECT_CALL(cm_.thread_local_cluster_.async_client_,
              send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(timeout)))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& /*message*/, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            return &request;
          }));

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});
  span->finishSpan();

  // Timer should be re-enabled.
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(1000), _));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.request_timeout", 5000U))
      .WillOnce(Return(5000U));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.flush_interval_ms", 1000U))
      .WillOnce(Return(1000U));

  timer_->invokeCallback();

  callback->onSuccess(request, makeSuccessResponse());

  EXPECT_EQ(1U, stats_.counter("tracing.lightstep.timer_flushed").value());
  EXPECT_EQ(1U, stats_.counter("tracing.lightstep.spans_sent").value());
  EXPECT_EQ(0U, stats_.counter("tracing.lightstep.reports_skipped_no_cluster").value());
}

TEST_F(LightStepDriverTest, CancelRequestOnDestruction) {
  setupValidDriver(1);

  Http::MockAsyncClientRequest request(&cm_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callback = nullptr;
  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));

  EXPECT_CALL(cm_.thread_local_cluster_.async_client_,
              send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(timeout)))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& /*message*/, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            return &request;
          }));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.request_timeout", 5000U))
      .WillOnce(Return(5000U));

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});
  span->finishSpan();

  driver_
      ->startSpan(config_, request_headers_, operation_name_, start_time_,
                  {Tracing::Reason::Sampling, true})
      ->finishSpan();

  EXPECT_CALL(request, cancel());

  driver_.reset();
}

TEST_F(LightStepDriverTest, SerializeAndDeserializeContext) {
  for (Common::Ot::OpenTracingDriver::PropagationMode propagation_mode :
       {Common::Ot::OpenTracingDriver::PropagationMode::SingleHeader,
        Common::Ot::OpenTracingDriver::PropagationMode::TracerNative}) {
    setupValidDriver(LightStepDriver::DefaultMinFlushSpans, propagation_mode);

    // Supply bogus context, that will be simply ignored.
    const std::string invalid_context = "notvalidcontext";
    request_headers_.setCopy(Http::CustomHeaders::get().OtSpanContext, invalid_context);
    stats_.counter("tracing.opentracing.span_context_extraction_error").reset();
    driver_->startSpan(config_, request_headers_, operation_name_, start_time_,
                       {Tracing::Reason::Sampling, true});
    EXPECT_EQ(1U, stats_.counter("tracing.opentracing.span_context_extraction_error").value());

    std::string injected_ctx(request_headers_.get_(Http::CustomHeaders::get().OtSpanContext));
    EXPECT_FALSE(injected_ctx.empty());

    // Supply empty context.
    request_headers_.remove(Http::CustomHeaders::get().OtSpanContext);
    Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                               start_time_, {Tracing::Reason::Sampling, true});

    EXPECT_FALSE(request_headers_.has(Http::CustomHeaders::get().OtSpanContext));
    span->injectContext(request_headers_);

    injected_ctx = std::string(request_headers_.get_(Http::CustomHeaders::get().OtSpanContext));
    EXPECT_FALSE(injected_ctx.empty());

    // Context can be parsed fine.
    const opentracing::Tracer& tracer = driver_->tracer();
    std::string context = Base64::decode(injected_ctx);
    std::istringstream iss{context, std::ios::binary};
    EXPECT_TRUE(tracer.Extract(iss));

    // Supply parent context, request_headers has properly populated x-ot-span-context.
    Tracing::SpanPtr span_with_parent = driver_->startSpan(
        config_, request_headers_, operation_name_, start_time_, {Tracing::Reason::Sampling, true});
    request_headers_.remove(Http::CustomHeaders::get().OtSpanContext);
    span_with_parent->injectContext(request_headers_);
    injected_ctx = std::string(request_headers_.get_(Http::CustomHeaders::get().OtSpanContext));
    EXPECT_FALSE(injected_ctx.empty());
  }
}

TEST_F(LightStepDriverTest, MultiplePropagationModes) {
  const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    propagation_modes:
    - ENVOY
    - LIGHTSTEP
    - B3
    - TRACE_CONTEXT
    )EOF";
  envoy::config::trace::v3::LightstepConfig lightstep_config;
  TestUtility::loadFromYaml(yaml_string, lightstep_config);

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.flush_interval_ms", _))
      .Times(AtLeast(1))
      .WillRepeatedly(Return(1000));

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.min_flush_spans",
                                             LightStepDriver::DefaultMinFlushSpans))
      .Times(AtLeast(1))
      .WillRepeatedly(Return(1));

  cm_.initializeClusters({"fake_cluster"}, {});
  ON_CALL(*cm_.active_clusters_["fake_cluster"]->info_, features())
      .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));
  setup(lightstep_config, true);

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});

  EXPECT_FALSE(request_headers_.has(Http::CustomHeaders::get().OtSpanContext));
  span->injectContext(request_headers_);
  EXPECT_TRUE(request_headers_.has(Http::CustomHeaders::get().OtSpanContext));
  EXPECT_TRUE(request_headers_.has("ot-tracer-traceid"));
  EXPECT_TRUE(request_headers_.has("x-b3-traceid"));
  EXPECT_TRUE(request_headers_.has("traceparent"));
}

TEST_F(LightStepDriverTest, SpawnChild) {
  setupValidDriver();

  Tracing::SpanPtr parent = driver_->startSpan(config_, request_headers_, operation_name_,
                                               start_time_, {Tracing::Reason::Sampling, true});
  parent->injectContext(request_headers_);

  Tracing::SpanPtr childViaHeaders = driver_->startSpan(
      config_, request_headers_, operation_name_, start_time_, {Tracing::Reason::Sampling, true});
  Tracing::SpanPtr childViaSpawn = parent->spawnChild(config_, operation_name_, start_time_);

  Http::TestRequestHeaderMapImpl base1{{":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}};
  Http::TestRequestHeaderMapImpl base2{{":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}};

  childViaHeaders->injectContext(base1);
  childViaSpawn->injectContext(base2);

  std::string base1_context =
      Base64::decode(std::string(base1.get_(Http::CustomHeaders::get().OtSpanContext)));
  std::string base2_context =
      Base64::decode(std::string(base2.get_(Http::CustomHeaders::get().OtSpanContext)));

  EXPECT_FALSE(base1_context.empty());
  EXPECT_FALSE(base2_context.empty());
}

TEST_F(LightStepDriverTest, GetAndSetBaggage) {
  setupValidDriver();
  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});

  std::string key = "key1";
  std::string value = "value1";
  span->setBaggage(key, value);
  EXPECT_EQ(span->getBaggage(key), value);
}

TEST_F(LightStepDriverTest, GetTraceId) {
  setupValidDriver();
  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});

  // This method is unimplemented and a noop.
  ASSERT_EQ(span->getTraceIdAsHex(), "");
}

} // namespace
} // namespace Lightstep
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
