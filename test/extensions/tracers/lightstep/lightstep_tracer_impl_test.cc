#include <chrono>
#include <memory>
#include <sstream>
#include <string>

#include "common/common/base64.h"
#include "common/grpc/common.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/runtime/runtime_impl.h"
#include "common/runtime/uuid_util.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/lightstep/lightstep_tracer_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AtLeast;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Lightstep {

class LightStepDriverTest : public testing::Test {
public:
  void setup(envoy::config::trace::v2::LightstepConfig& lightstep_config, bool init_timer,
             Common::Ot::OpenTracingDriver::PropagationMode propagation_mode =
                 Common::Ot::OpenTracingDriver::PropagationMode::TracerNative) {
    std::unique_ptr<lightstep::LightStepTracerOptions> opts(
        new lightstep::LightStepTracerOptions());
    opts->access_token = "sample_token";
    opts->component_name = "component";

    ON_CALL(cm_, httpAsyncClientForCluster("fake_cluster"))
        .WillByDefault(ReturnRef(cm_.async_client_));

    if (init_timer) {
      timer_ = new NiceMock<Event::MockTimer>(&tls_.dispatcher_);
      EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(1000)));
    }

    driver_ = std::make_unique<LightStepDriver>(lightstep_config, cm_, stats_, tls_, runtime_,
                                                std::move(opts), propagation_mode);
  }

  void setupValidDriver(Common::Ot::OpenTracingDriver::PropagationMode propagation_mode =
                            Common::Ot::OpenTracingDriver::PropagationMode::TracerNative) {
    EXPECT_CALL(cm_, get("fake_cluster")).WillRepeatedly(Return(&cm_.thread_local_cluster_));
    ON_CALL(*cm_.thread_local_cluster_.cluster_.info_, features())
        .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));

    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    )EOF";
    envoy::config::trace::v2::LightstepConfig lightstep_config;
    MessageUtil::loadFromYaml(yaml_string, lightstep_config);

    setup(lightstep_config, true, propagation_mode);
  }

  const std::string operation_name_{"test"};
  Http::TestHeaderMapImpl request_headers_{
      {":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}};
  const Http::TestHeaderMapImpl response_headers_{{":status", "500"}};
  SystemTime start_time_;
  StreamInfo::MockStreamInfo stream_info_;

  NiceMock<ThreadLocal::MockInstance> tls_;
  std::unique_ptr<LightStepDriver> driver_;
  NiceMock<Event::MockTimer>* timer_;
  Stats::IsolatedStoreImpl stats_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Runtime::MockRandomGenerator> random_;
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
    envoy::config::trace::v2::LightstepConfig lightstep_config;

    EXPECT_THROW(setup(lightstep_config, false), EnvoyException);
  }

  {
    // Valid config but not valid cluster.
    EXPECT_CALL(cm_, get("fake_cluster")).WillOnce(Return(nullptr));

    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    )EOF";
    envoy::config::trace::v2::LightstepConfig lightstep_config;
    MessageUtil::loadFromYaml(yaml_string, lightstep_config);

    EXPECT_THROW(setup(lightstep_config, false), EnvoyException);
  }

  {
    // Valid config, but upstream cluster does not support http2.
    EXPECT_CALL(cm_, get("fake_cluster")).WillRepeatedly(Return(&cm_.thread_local_cluster_));
    ON_CALL(*cm_.thread_local_cluster_.cluster_.info_, features()).WillByDefault(Return(0));

    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    )EOF";
    envoy::config::trace::v2::LightstepConfig lightstep_config;
    MessageUtil::loadFromYaml(yaml_string, lightstep_config);

    EXPECT_THROW(setup(lightstep_config, false), EnvoyException);
  }

  {
    EXPECT_CALL(cm_, get("fake_cluster")).WillRepeatedly(Return(&cm_.thread_local_cluster_));
    ON_CALL(*cm_.thread_local_cluster_.cluster_.info_, features())
        .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));

    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    )EOF";
    envoy::config::trace::v2::LightstepConfig lightstep_config;
    MessageUtil::loadFromYaml(yaml_string, lightstep_config);

    setup(lightstep_config, true);
  }
}

TEST_F(LightStepDriverTest, FlushSeveralSpans) {
  setupValidDriver();

  Http::MockAsyncClientRequest request(&cm_.async_client_);
  Http::AsyncClient::Callbacks* callback;
  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));

  EXPECT_CALL(cm_.async_client_,
              send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(timeout)))
      .WillOnce(
          Invoke([&](Http::MessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            EXPECT_STREQ("/lightstep.collector.CollectorService/Report",
                         message->headers().Path()->value().c_str());
            EXPECT_STREQ("fake_cluster", message->headers().Host()->value().c_str());
            EXPECT_STREQ("application/grpc", message->headers().ContentType()->value().c_str());

            return &request;
          }));

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.min_flush_spans",
                                             LightStepDriver::DefaultMinFlushSpans))
      .Times(AtLeast(1))
      .WillRepeatedly(Return(2));
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

  Http::MessagePtr msg(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));

  msg->trailers(Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{"grpc-status", "0"}}});
  std::unique_ptr<Protobuf::Message> collector_response =
      lightstep::Transporter::MakeCollectorResponse();
  EXPECT_NE(collector_response, nullptr);
  msg->body() = Grpc::Common::serializeBody(*collector_response);

  callback->onSuccess(std::move(msg));

  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("grpc.lightstep.collector.CollectorService.Report.success")
                    .value());

  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("grpc.lightstep.collector.CollectorService.Report.total")
                    .value());
  EXPECT_EQ(2U, stats_.counter("tracing.lightstep.spans_sent").value());
}

TEST_F(LightStepDriverTest, FlushOneFailure) {
  setupValidDriver();

  Http::MockAsyncClientRequest request(&cm_.async_client_);
  Http::AsyncClient::Callbacks* callback;
  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));

  EXPECT_CALL(cm_.async_client_,
              send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(timeout)))
      .WillOnce(
          Invoke([&](Http::MessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            EXPECT_STREQ("/lightstep.collector.CollectorService/Report",
                         message->headers().Path()->value().c_str());
            EXPECT_STREQ("fake_cluster", message->headers().Host()->value().c_str());
            EXPECT_STREQ("application/grpc", message->headers().ContentType()->value().c_str());

            return &request;
          }));

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.min_flush_spans",
                                             LightStepDriver::DefaultMinFlushSpans))
      .WillOnce(Return(1));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.request_timeout", 5000U))
      .WillOnce(Return(5000U));

  Tracing::SpanPtr first_span = driver_->startSpan(config_, request_headers_, operation_name_,
                                                   start_time_, {Tracing::Reason::Sampling, true});

  first_span->finishSpan();

  callback->onFailure(Http::AsyncClient::FailureReason::Reset);

  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("grpc.lightstep.collector.CollectorService.Report.failure")
                    .value());
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("grpc.lightstep.collector.CollectorService.Report.total")
                    .value());
  EXPECT_EQ(1U, stats_.counter("tracing.lightstep.spans_sent").value());
}

TEST_F(LightStepDriverTest, FlushOneInvalidResponse) {
  setupValidDriver();

  Http::MockAsyncClientRequest request(&cm_.async_client_);
  Http::AsyncClient::Callbacks* callback;
  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));

  EXPECT_CALL(cm_.async_client_,
              send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(timeout)))
      .WillOnce(
          Invoke([&](Http::MessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            EXPECT_STREQ("/lightstep.collector.CollectorService/Report",
                         message->headers().Path()->value().c_str());
            EXPECT_STREQ("fake_cluster", message->headers().Host()->value().c_str());
            EXPECT_STREQ("application/grpc", message->headers().ContentType()->value().c_str());

            return &request;
          }));

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.min_flush_spans",
                                             LightStepDriver::DefaultMinFlushSpans))
      .WillOnce(Return(1));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.request_timeout", 5000U))
      .WillOnce(Return(5000U));

  Tracing::SpanPtr first_span = driver_->startSpan(config_, request_headers_, operation_name_,
                                                   start_time_, {Tracing::Reason::Sampling, true});

  first_span->finishSpan();

  Http::MessagePtr msg(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));

  msg->trailers(Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{"grpc-status", "0"}}});
  msg->body() = std::make_unique<Buffer::OwnedImpl>("invalidresponse");

  callback->onSuccess(std::move(msg));

  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("grpc.lightstep.collector.CollectorService.Report.failure")
                    .value());
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("grpc.lightstep.collector.CollectorService.Report.total")
                    .value());
  EXPECT_EQ(1U, stats_.counter("tracing.lightstep.spans_sent").value());
}

TEST_F(LightStepDriverTest, FlushSpansTimer) {
  setupValidDriver();

  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));
  EXPECT_CALL(cm_.async_client_,
              send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(timeout)));

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.min_flush_spans",
                                             LightStepDriver::DefaultMinFlushSpans))
      .WillOnce(Return(LightStepDriver::DefaultMinFlushSpans));

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});
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
  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));

  EXPECT_CALL(cm_.async_client_,
              send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(timeout)))
      .WillOnce(
          Invoke([&](Http::MessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            EXPECT_STREQ("/lightstep.collector.CollectorService/Report",
                         message->headers().Path()->value().c_str());
            EXPECT_STREQ("fake_cluster", message->headers().Host()->value().c_str());
            EXPECT_STREQ("application/grpc", message->headers().ContentType()->value().c_str());

            return &request;
          }));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.min_flush_spans",
                                             LightStepDriver::DefaultMinFlushSpans))
      .WillOnce(Return(1));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.request_timeout", 5000U))
      .WillOnce(Return(5000U));

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});
  span->finishSpan();

  Http::MessagePtr msg(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));

  // No trailers, gRPC is considered failed.
  callback->onSuccess(std::move(msg));

  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("grpc.lightstep.collector.CollectorService.Report.failure")
                    .value());
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("grpc.lightstep.collector.CollectorService.Report.total")
                    .value());
  EXPECT_EQ(1U, stats_.counter("tracing.lightstep.spans_sent").value());
}

TEST_F(LightStepDriverTest, CancelRequestOnDestruction) {
  setupValidDriver();

  Http::MockAsyncClientRequest request(&cm_.async_client_);
  Http::AsyncClient::Callbacks* callback;
  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));

  EXPECT_CALL(cm_.async_client_,
              send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(timeout)))
      .WillOnce(
          Invoke([&](Http::MessagePtr& /*message*/, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            return &request;
          }));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.min_flush_spans",
                                             LightStepDriver::DefaultMinFlushSpans))
      .WillOnce(Return(1));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.lightstep.request_timeout", 5000U))
      .WillOnce(Return(5000U));

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});
  span->finishSpan();

  EXPECT_CALL(request, cancel());

  driver_.reset();
}

TEST_F(LightStepDriverTest, SerializeAndDeserializeContext) {
  for (Common::Ot::OpenTracingDriver::PropagationMode propagation_mode :
       {Common::Ot::OpenTracingDriver::PropagationMode::SingleHeader,
        Common::Ot::OpenTracingDriver::PropagationMode::TracerNative}) {
    setupValidDriver(propagation_mode);

    // Supply bogus context, that will be simply ignored.
    const std::string invalid_context = "notvalidcontext";
    request_headers_.insertOtSpanContext().value(invalid_context);
    stats_.counter("tracing.opentracing.span_context_extraction_error").reset();
    driver_->startSpan(config_, request_headers_, operation_name_, start_time_,
                       {Tracing::Reason::Sampling, true});
    EXPECT_EQ(1U, stats_.counter("tracing.opentracing.span_context_extraction_error").value());

    std::string injected_ctx = request_headers_.OtSpanContext()->value().c_str();
    EXPECT_FALSE(injected_ctx.empty());

    // Supply empty context.
    request_headers_.removeOtSpanContext();
    Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                               start_time_, {Tracing::Reason::Sampling, true});

    EXPECT_EQ(nullptr, request_headers_.OtSpanContext());
    span->injectContext(request_headers_);

    injected_ctx = request_headers_.OtSpanContext()->value().c_str();
    EXPECT_FALSE(injected_ctx.empty());

    // Context can be parsed fine.
    const opentracing::Tracer& tracer = driver_->tracer();
    std::string context = Base64::decode(injected_ctx);
    std::istringstream iss{context, std::ios::binary};
    EXPECT_TRUE(tracer.Extract(iss));

    // Supply parent context, request_headers has properly populated x-ot-span-context.
    Tracing::SpanPtr span_with_parent = driver_->startSpan(
        config_, request_headers_, operation_name_, start_time_, {Tracing::Reason::Sampling, true});
    request_headers_.removeOtSpanContext();
    span_with_parent->injectContext(request_headers_);
    injected_ctx = request_headers_.OtSpanContext()->value().c_str();
    EXPECT_FALSE(injected_ctx.empty());
  }
}

TEST_F(LightStepDriverTest, SpawnChild) {
  setupValidDriver();

  Tracing::SpanPtr parent = driver_->startSpan(config_, request_headers_, operation_name_,
                                               start_time_, {Tracing::Reason::Sampling, true});
  parent->injectContext(request_headers_);

  Tracing::SpanPtr childViaHeaders = driver_->startSpan(
      config_, request_headers_, operation_name_, start_time_, {Tracing::Reason::Sampling, true});
  Tracing::SpanPtr childViaSpawn = parent->spawnChild(config_, operation_name_, start_time_);

  Http::TestHeaderMapImpl base1{{":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}};
  Http::TestHeaderMapImpl base2{{":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}};

  childViaHeaders->injectContext(base1);
  childViaSpawn->injectContext(base2);

  std::string base1_context = Base64::decode(base1.OtSpanContext()->value().c_str());
  std::string base2_context = Base64::decode(base2.OtSpanContext()->value().c_str());

  EXPECT_FALSE(base1_context.empty());
  EXPECT_FALSE(base2_context.empty());
}

} // namespace Lightstep
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
