#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/runtime/runtime_impl.h"
#include "common/runtime/uuid_util.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/zipkin/zipkin_core_constants.h"
#include "extensions/tracers/zipkin/zipkin_tracer_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::Test;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

class ZipkinDriverTest : public Test {
public:
  ZipkinDriverTest() : time_source_(test_time_.timeSystem()) {}

  void setup(envoy::config::trace::v2::ZipkinConfig& zipkin_config, bool init_timer) {
    ON_CALL(cm_, httpAsyncClientForCluster("fake_cluster"))
        .WillByDefault(ReturnRef(cm_.async_client_));

    if (init_timer) {
      timer_ = new NiceMock<Event::MockTimer>(&tls_.dispatcher_);
      EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5000)));
    }

    driver_ = std::make_unique<Driver>(zipkin_config, cm_, stats_, tls_, runtime_, local_info_,
                                       random_, time_source_);
  }

  void setupValidDriver() {
    EXPECT_CALL(cm_, get("fake_cluster")).WillRepeatedly(Return(&cm_.thread_local_cluster_));

    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    collector_endpoint: /api/v1/spans
    )EOF";
    envoy::config::trace::v2::ZipkinConfig zipkin_config;
    MessageUtil::loadFromYaml(yaml_string, zipkin_config);

    setup(zipkin_config, true);
  }

  // TODO(#4160): Currently time_system_ is initialized from DangerousDeprecatedTestTime, which uses
  // real time, not mock-time. When that is switched to use mock-time intead, I think
  // generateRandom64() may not be as random as we want, and we'll need to inject entropy
  // appropriate for the test.
  uint64_t generateRandom64() { return Util::generateRandom64(time_source_); }

  const std::string operation_name_{"test"};
  Http::TestHeaderMapImpl request_headers_{
      {":authority", "api.lyft.com"}, {":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}};
  SystemTime start_time_;
  StreamInfo::MockStreamInfo stream_info_;

  NiceMock<ThreadLocal::MockInstance> tls_;
  std::unique_ptr<Driver> driver_;
  NiceMock<Event::MockTimer>* timer_;
  Stats::IsolatedStoreImpl stats_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Runtime::MockRandomGenerator> random_;

  NiceMock<Tracing::MockConfig> config_;
  DangerousDeprecatedTestTime test_time_;
  TimeSource& time_source_;
};

TEST_F(ZipkinDriverTest, InitializeDriver) {
  {
    // Empty config
    envoy::config::trace::v2::ZipkinConfig zipkin_config;

    EXPECT_THROW(setup(zipkin_config, false), EnvoyException);
  }

  {
    // Valid config but not valid cluster.
    EXPECT_CALL(cm_, get("fake_cluster")).WillOnce(Return(nullptr));
    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    collector_endpoint: /api/v1/spans
    )EOF";
    envoy::config::trace::v2::ZipkinConfig zipkin_config;
    MessageUtil::loadFromYaml(yaml_string, zipkin_config);

    EXPECT_THROW(setup(zipkin_config, false), EnvoyException);
  }

  {
    // valid config
    EXPECT_CALL(cm_, get("fake_cluster")).WillRepeatedly(Return(&cm_.thread_local_cluster_));
    ON_CALL(*cm_.thread_local_cluster_.cluster_.info_, features()).WillByDefault(Return(0));

    const std::string yaml_string = R"EOF(
    collector_cluster: fake_cluster
    collector_endpoint: /api/v1/spans
    )EOF";
    envoy::config::trace::v2::ZipkinConfig zipkin_config;
    MessageUtil::loadFromYaml(yaml_string, zipkin_config);

    setup(zipkin_config, true);
  }
}

TEST_F(ZipkinDriverTest, FlushSeveralSpans) {
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

            EXPECT_STREQ("/api/v1/spans", message->headers().Path()->value().c_str());
            EXPECT_STREQ("fake_cluster", message->headers().Host()->value().c_str());
            EXPECT_STREQ("application/json", message->headers().ContentType()->value().c_str());

            return &request;
          }));

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.min_flush_spans", 5))
      .Times(2)
      .WillRepeatedly(Return(2));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.request_timeout", 5000U))
      .WillOnce(Return(5000U));

  Tracing::SpanPtr first_span = driver_->startSpan(config_, request_headers_, operation_name_,
                                                   start_time_, {Tracing::Reason::Sampling, true});
  first_span->finishSpan();

  Tracing::SpanPtr second_span = driver_->startSpan(config_, request_headers_, operation_name_,
                                                    start_time_, {Tracing::Reason::Sampling, true});
  second_span->finishSpan();

  Http::MessagePtr msg(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "202"}}}));

  callback->onSuccess(std::move(msg));

  EXPECT_EQ(2U, stats_.counter("tracing.zipkin.spans_sent").value());
  EXPECT_EQ(1U, stats_.counter("tracing.zipkin.reports_sent").value());
  EXPECT_EQ(0U, stats_.counter("tracing.zipkin.reports_dropped").value());
  EXPECT_EQ(0U, stats_.counter("tracing.zipkin.reports_failed").value());

  callback->onFailure(Http::AsyncClient::FailureReason::Reset);

  EXPECT_EQ(1U, stats_.counter("tracing.zipkin.reports_failed").value());
}

TEST_F(ZipkinDriverTest, FlushOneSpanReportFailure) {
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

            EXPECT_STREQ("/api/v1/spans", message->headers().Path()->value().c_str());
            EXPECT_STREQ("fake_cluster", message->headers().Host()->value().c_str());
            EXPECT_STREQ("application/json", message->headers().ContentType()->value().c_str());

            return &request;
          }));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.min_flush_spans", 5))
      .WillOnce(Return(1));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.request_timeout", 5000U))
      .WillOnce(Return(5000U));

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});
  span->finishSpan();

  Http::MessagePtr msg(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "404"}}}));

  // AsyncClient can fail with valid HTTP headers
  callback->onSuccess(std::move(msg));

  EXPECT_EQ(1U, stats_.counter("tracing.zipkin.spans_sent").value());
  EXPECT_EQ(0U, stats_.counter("tracing.zipkin.reports_sent").value());
  EXPECT_EQ(1U, stats_.counter("tracing.zipkin.reports_dropped").value());
  EXPECT_EQ(0U, stats_.counter("tracing.zipkin.reports_failed").value());
}

TEST_F(ZipkinDriverTest, FlushSpansTimer) {
  setupValidDriver();

  const absl::optional<std::chrono::milliseconds> timeout(std::chrono::seconds(5));
  EXPECT_CALL(cm_.async_client_,
              send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(timeout)));

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.min_flush_spans", 5))
      .WillOnce(Return(5));

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});
  span->finishSpan();

  // Timer should be re-enabled.
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5000)));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.request_timeout", 5000U))
      .WillOnce(Return(5000U));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.zipkin.flush_interval_ms", 5000U))
      .WillOnce(Return(5000U));

  timer_->callback_();

  EXPECT_EQ(1U, stats_.counter("tracing.zipkin.timer_flushed").value());
  EXPECT_EQ(1U, stats_.counter("tracing.zipkin.spans_sent").value());
}

TEST_F(ZipkinDriverTest, NoB3ContextSampledTrue) {
  setupValidDriver();

  EXPECT_EQ(nullptr, request_headers_.get(ZipkinCoreConstants::get().X_B3_SPAN_ID));
  EXPECT_EQ(nullptr, request_headers_.get(ZipkinCoreConstants::get().X_B3_TRACE_ID));
  EXPECT_EQ(nullptr, request_headers_.get(ZipkinCoreConstants::get().X_B3_SAMPLED));

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});

  ZipkinSpanPtr zipkin_span(dynamic_cast<ZipkinSpan*>(span.release()));
  EXPECT_TRUE(zipkin_span->span().sampled());
}

TEST_F(ZipkinDriverTest, NoB3ContextSampledFalse) {
  setupValidDriver();

  EXPECT_EQ(nullptr, request_headers_.get(ZipkinCoreConstants::get().X_B3_SPAN_ID));
  EXPECT_EQ(nullptr, request_headers_.get(ZipkinCoreConstants::get().X_B3_TRACE_ID));
  EXPECT_EQ(nullptr, request_headers_.get(ZipkinCoreConstants::get().X_B3_SAMPLED));

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, false});

  ZipkinSpanPtr zipkin_span(dynamic_cast<ZipkinSpan*>(span.release()));
  EXPECT_FALSE(zipkin_span->span().sampled());
}

TEST_F(ZipkinDriverTest, PropagateB3NoSampleDecisionSampleTrue) {
  setupValidDriver();

  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_TRACE_ID,
                                   Hex::uint64ToHex(generateRandom64()));
  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_SPAN_ID,
                                   Hex::uint64ToHex(generateRandom64()));
  EXPECT_EQ(nullptr, request_headers_.get(ZipkinCoreConstants::get().X_B3_SAMPLED));

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});

  ZipkinSpanPtr zipkin_span(dynamic_cast<ZipkinSpan*>(span.release()));
  EXPECT_TRUE(zipkin_span->span().sampled());
}

TEST_F(ZipkinDriverTest, PropagateB3NoSampleDecisionSampleFalse) {
  setupValidDriver();

  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_TRACE_ID,
                                   Hex::uint64ToHex(generateRandom64()));
  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_SPAN_ID,
                                   Hex::uint64ToHex(generateRandom64()));
  EXPECT_EQ(nullptr, request_headers_.get(ZipkinCoreConstants::get().X_B3_SAMPLED));

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, false});

  ZipkinSpanPtr zipkin_span(dynamic_cast<ZipkinSpan*>(span.release()));
  EXPECT_FALSE(zipkin_span->span().sampled());
}

TEST_F(ZipkinDriverTest, PropagateB3NotSampled) {
  setupValidDriver();

  EXPECT_EQ(nullptr, request_headers_.get(ZipkinCoreConstants::get().X_B3_SPAN_ID));
  EXPECT_EQ(nullptr, request_headers_.get(ZipkinCoreConstants::get().X_B3_TRACE_ID));

  // Only context header set is B3 sampled to indicate trace should not be sampled
  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_SAMPLED,
                                   ZipkinCoreConstants::get().NOT_SAMPLED);
  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});

  request_headers_.remove(ZipkinCoreConstants::get().X_B3_SAMPLED);

  span->injectContext(request_headers_);

  auto sampled_entry = request_headers_.get(ZipkinCoreConstants::get().X_B3_SAMPLED);

  // Check B3 sampled flag is set to not sample
  EXPECT_EQ(ZipkinCoreConstants::get().NOT_SAMPLED, sampled_entry->value().getStringView());
}

TEST_F(ZipkinDriverTest, PropagateB3NotSampledWithFalse) {
  setupValidDriver();

  EXPECT_EQ(nullptr, request_headers_.get(ZipkinCoreConstants::get().X_B3_SPAN_ID));
  EXPECT_EQ(nullptr, request_headers_.get(ZipkinCoreConstants::get().X_B3_TRACE_ID));

  // Only context header set is B3 sampled to indicate trace should not be sampled (using legacy
  // 'false' value)
  const std::string sampled = "false";
  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_SAMPLED, sampled);
  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});

  request_headers_.remove(ZipkinCoreConstants::get().X_B3_SAMPLED);

  span->injectContext(request_headers_);

  auto sampled_entry = request_headers_.get(ZipkinCoreConstants::get().X_B3_SAMPLED);
  // Check B3 sampled flag is set to not sample
  EXPECT_EQ(ZipkinCoreConstants::get().NOT_SAMPLED, sampled_entry->value().getStringView());
}

TEST_F(ZipkinDriverTest, PropagateB3SampledWithTrue) {
  setupValidDriver();

  EXPECT_EQ(nullptr, request_headers_.get(ZipkinCoreConstants::get().X_B3_SPAN_ID));
  EXPECT_EQ(nullptr, request_headers_.get(ZipkinCoreConstants::get().X_B3_TRACE_ID));

  // Only context header set is B3 sampled to indicate trace should be sampled (using legacy
  // 'true' value)
  const std::string sampled = "true";
  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_SAMPLED, sampled);
  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, false});

  request_headers_.remove(ZipkinCoreConstants::get().X_B3_SAMPLED);

  span->injectContext(request_headers_);

  auto sampled_entry = request_headers_.get(ZipkinCoreConstants::get().X_B3_SAMPLED);
  // Check B3 sampled flag is set to sample
  EXPECT_EQ(ZipkinCoreConstants::get().SAMPLED, sampled_entry->value().getStringView());
}

TEST_F(ZipkinDriverTest, PropagateB3SampleFalse) {
  setupValidDriver();

  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_TRACE_ID,
                                   Hex::uint64ToHex(generateRandom64()));
  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_SPAN_ID,
                                   Hex::uint64ToHex(generateRandom64()));
  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_SAMPLED,
                                   ZipkinCoreConstants::get().NOT_SAMPLED);

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});

  ZipkinSpanPtr zipkin_span(dynamic_cast<ZipkinSpan*>(span.release()));
  EXPECT_FALSE(zipkin_span->span().sampled());
}

TEST_F(ZipkinDriverTest, ZipkinSpanTest) {
  setupValidDriver();

  // ====
  // Test effective setTag()
  // ====

  request_headers_.removeOtSpanContext();

  // New span will have a CS annotation
  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});

  ZipkinSpanPtr zipkin_span(dynamic_cast<ZipkinSpan*>(span.release()));
  zipkin_span->setTag("key", "value");

  Span& zipkin_zipkin_span = zipkin_span->span();
  EXPECT_EQ(1ULL, zipkin_zipkin_span.binaryAnnotations().size());
  EXPECT_EQ("key", zipkin_zipkin_span.binaryAnnotations()[0].key());
  EXPECT_EQ("value", zipkin_zipkin_span.binaryAnnotations()[0].value());

  // ====
  // Test setTag() with SR annotated span
  // ====

  const std::string trace_id = Hex::uint64ToHex(generateRandom64());
  const std::string span_id = Hex::uint64ToHex(generateRandom64());
  const std::string parent_id = Hex::uint64ToHex(generateRandom64());
  const std::string context =
      trace_id + ";" + span_id + ";" + parent_id + ";" + ZipkinCoreConstants::get().CLIENT_SEND;

  request_headers_.insertOtSpanContext().value(context);

  // New span will have an SR annotation
  Tracing::SpanPtr span2 = driver_->startSpan(config_, request_headers_, operation_name_,
                                              start_time_, {Tracing::Reason::Sampling, true});

  ZipkinSpanPtr zipkin_span2(dynamic_cast<ZipkinSpan*>(span2.release()));
  zipkin_span2->setTag("key2", "value2");

  Span& zipkin_zipkin_span2 = zipkin_span2->span();
  EXPECT_EQ(1ULL, zipkin_zipkin_span2.binaryAnnotations().size());
  EXPECT_EQ("key2", zipkin_zipkin_span2.binaryAnnotations()[0].key());
  EXPECT_EQ("value2", zipkin_zipkin_span2.binaryAnnotations()[0].value());

  // ====
  // Test setTag() with empty annotations vector
  // ====
  Tracing::SpanPtr span3 = driver_->startSpan(config_, request_headers_, operation_name_,
                                              start_time_, {Tracing::Reason::Sampling, true});
  ZipkinSpanPtr zipkin_span3(dynamic_cast<ZipkinSpan*>(span3.release()));
  Span& zipkin_zipkin_span3 = zipkin_span3->span();

  std::vector<Annotation> annotations;
  zipkin_zipkin_span3.setAnnotations(annotations);

  zipkin_span3->setTag("key3", "value3");
  EXPECT_EQ(1ULL, zipkin_zipkin_span3.binaryAnnotations().size());
  EXPECT_EQ("key3", zipkin_zipkin_span3.binaryAnnotations()[0].key());
  EXPECT_EQ("value3", zipkin_zipkin_span3.binaryAnnotations()[0].value());
}

TEST_F(ZipkinDriverTest, ZipkinSpanContextFromB3HeadersTest) {
  setupValidDriver();

  const std::string trace_id = Hex::uint64ToHex(generateRandom64());
  const std::string span_id = Hex::uint64ToHex(generateRandom64());
  const std::string parent_id = Hex::uint64ToHex(generateRandom64());

  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_TRACE_ID, trace_id);
  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_SPAN_ID, span_id);
  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_PARENT_SPAN_ID, parent_id);

  // New span will have an SR annotation - so its span and parent ids will be
  // the same as the supplied span context (i.e. shared context)
  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});

  ZipkinSpanPtr zipkin_span(dynamic_cast<ZipkinSpan*>(span.release()));

  EXPECT_EQ(trace_id, zipkin_span->span().traceIdAsHexString());
  EXPECT_EQ(span_id, zipkin_span->span().idAsHexString());
  EXPECT_EQ(parent_id, zipkin_span->span().parentIdAsHexString());
  EXPECT_TRUE(zipkin_span->span().sampled());
}

TEST_F(ZipkinDriverTest, ZipkinSpanContextFromB3Headers128TraceIdTest) {
  setupValidDriver();

  const uint64_t trace_id_high = generateRandom64();
  const uint64_t trace_id_low = generateRandom64();
  const std::string trace_id = Hex::uint64ToHex(trace_id_high) + Hex::uint64ToHex(trace_id_low);
  const std::string span_id = Hex::uint64ToHex(generateRandom64());
  const std::string parent_id = Hex::uint64ToHex(generateRandom64());

  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_TRACE_ID, trace_id);
  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_SPAN_ID, span_id);
  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_PARENT_SPAN_ID, parent_id);

  // New span will have an SR annotation - so its span and parent ids will be
  // the same as the supplied span context (i.e. shared context)
  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});

  ZipkinSpanPtr zipkin_span(dynamic_cast<ZipkinSpan*>(span.release()));

  EXPECT_EQ(trace_id_high, zipkin_span->span().traceIdHigh());
  EXPECT_EQ(trace_id_low, zipkin_span->span().traceId());
  EXPECT_EQ(trace_id, zipkin_span->span().traceIdAsHexString());
  EXPECT_EQ(span_id, zipkin_span->span().idAsHexString());
  EXPECT_EQ(parent_id, zipkin_span->span().parentIdAsHexString());
  EXPECT_TRUE(zipkin_span->span().sampled());
}

TEST_F(ZipkinDriverTest, ZipkinSpanContextFromInvalidTraceIdB3HeadersTest) {
  setupValidDriver();

  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_TRACE_ID, std::string("xyz"));
  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_SPAN_ID,
                                   Hex::uint64ToHex(generateRandom64()));
  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_PARENT_SPAN_ID,
                                   Hex::uint64ToHex(generateRandom64()));

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});
  EXPECT_NE(nullptr, dynamic_cast<Tracing::NullSpan*>(span.get()));
}

TEST_F(ZipkinDriverTest, ZipkinSpanContextFromInvalidSpanIdB3HeadersTest) {
  setupValidDriver();

  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_TRACE_ID,
                                   Hex::uint64ToHex(generateRandom64()));
  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_SPAN_ID, std::string("xyz"));
  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_PARENT_SPAN_ID,
                                   Hex::uint64ToHex(generateRandom64()));

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});
  EXPECT_NE(nullptr, dynamic_cast<Tracing::NullSpan*>(span.get()));
}

TEST_F(ZipkinDriverTest, ZipkinSpanContextFromInvalidParentIdB3HeadersTest) {
  setupValidDriver();

  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_TRACE_ID,
                                   Hex::uint64ToHex(generateRandom64()));
  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_SPAN_ID,
                                   Hex::uint64ToHex(generateRandom64()));
  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_PARENT_SPAN_ID,
                                   std::string("xyz"));

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});
  EXPECT_NE(nullptr, dynamic_cast<Tracing::NullSpan*>(span.get()));
}

TEST_F(ZipkinDriverTest, ExplicitlySetSampledFalse) {
  setupValidDriver();

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, true});

  span->setSampled(false);

  request_headers_.remove(ZipkinCoreConstants::get().X_B3_SAMPLED);

  span->injectContext(request_headers_);

  auto sampled_entry = request_headers_.get(ZipkinCoreConstants::get().X_B3_SAMPLED);
  // Check B3 sampled flag is set to not sample
  EXPECT_EQ(ZipkinCoreConstants::get().NOT_SAMPLED, sampled_entry->value().getStringView());
}

TEST_F(ZipkinDriverTest, ExplicitlySetSampledTrue) {
  setupValidDriver();

  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, false});

  span->setSampled(true);

  request_headers_.remove(ZipkinCoreConstants::get().X_B3_SAMPLED);

  span->injectContext(request_headers_);

  auto sampled_entry = request_headers_.get(ZipkinCoreConstants::get().X_B3_SAMPLED);
  // Check B3 sampled flag is set to sample
  EXPECT_EQ(ZipkinCoreConstants::get().SAMPLED, sampled_entry->value().getStringView());
}

TEST_F(ZipkinDriverTest, DuplicatedHeader) {
  setupValidDriver();
  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_TRACE_ID,
                                   Hex::uint64ToHex(generateRandom64()));
  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_SPAN_ID,
                                   Hex::uint64ToHex(generateRandom64()));
  request_headers_.addReferenceKey(ZipkinCoreConstants::get().X_B3_PARENT_SPAN_ID,
                                   Hex::uint64ToHex(generateRandom64()));
  Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                             start_time_, {Tracing::Reason::Sampling, false});

  typedef std::function<bool(const std::string& key)> DupCallback;
  DupCallback dup_callback = [](const std::string& key) -> bool {
    static std::unordered_map<std::string, bool> dup;
    if (dup.find(key) == dup.end()) {
      dup[key] = true;
      return false;
    }
    return true;
  };

  span->setSampled(true);
  span->injectContext(request_headers_);
  request_headers_.iterate(
      [](const Http::HeaderEntry& header, void* cb) -> Http::HeaderMap::Iterate {
        EXPECT_FALSE(static_cast<DupCallback*>(cb)->operator()(header.key().c_str()));
        return Http::HeaderMap::Iterate::Continue;
      },
      &dup_callback);
}
} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
