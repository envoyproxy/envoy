#include <string>

#include "source/extensions/tracers/xray/tracer.h"
#include "source/extensions/tracers/xray/xray_configuration.h"
#include "source/extensions/tracers/xray/xray_tracer_impl.h"

#include "test/mocks/server/tracer_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

namespace {

class XRayDriverTest : public ::testing::Test {
public:
  XRayDriverTest() {
    // To ensure the start_time_ is set to zero for each test to avoid flakiness.
    stream_info_.start_time_ = Envoy::SystemTime{};
    // To ensure the monotonicTime is set to value larger than 1s for each test
    // to avoid flakiness.
    stream_info_.ts_.setMonotonicTime(std::chrono::seconds(2048));
  }

  const std::string operation_name_ = "test_operation_name";
  // The MockStreamInfo will register the singleton time system to SimulatedTimeSystem and ignore
  // the TestRealTimeSystem in the MockTracerFactoryContext.
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  absl::flat_hash_map<std::string, ProtobufWkt::Value> aws_metadata_;
  NiceMock<Server::Configuration::MockTracerFactoryContext> context_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Tracing::MockConfig> tracing_config_;
  Tracing::TestTraceContextImpl request_headers_{
      {":authority", "api.amazon.com"}, {":path", "/"}, {":method", "GET"}};
};

TEST_F(XRayDriverTest, XRayTraceHeaderNotSampled) {
  request_headers_.set(xRayTraceHeader().key(), "Root=1-272793;Parent=5398ad8;Sampled=0");

  XRayConfiguration config{"" /*daemon_endpoint*/, "test_segment_name", "" /*sampling_rules*/,
                           "" /*origin*/, aws_metadata_};
  Driver driver(config, context_);

  Tracing::Decision tracing_decision{Tracing::Reason::Sampling, false /*sampled*/};

  auto span = driver.startSpan(tracing_config_, request_headers_, stream_info_, operation_name_,
                               tracing_decision);
  ASSERT_NE(span, nullptr);
  auto* xray_span = static_cast<XRay::Span*>(span.get());
  ASSERT_FALSE(xray_span->sampled());
}

TEST_F(XRayDriverTest, XRayTraceHeaderSampled) {
  request_headers_.set(xRayTraceHeader().key(), "Root=1-272793;Parent=5398ad8;Sampled=1");

  XRayConfiguration config{"" /*daemon_endpoint*/, "test_segment_name", "" /*sampling_rules*/,
                           "" /*origin*/, aws_metadata_};
  Driver driver(config, context_);

  Tracing::Decision tracing_decision{Tracing::Reason::Sampling, false /*sampled*/};

  auto span = driver.startSpan(tracing_config_, request_headers_, stream_info_, operation_name_,
                               tracing_decision);
  ASSERT_NE(span, nullptr);
}

TEST_F(XRayDriverTest, XRayTraceHeaderSamplingUnknown) {
  request_headers_.set(xRayTraceHeader().key(), "Root=1-272793;Parent=5398ad8;Sampled=");

  XRayConfiguration config{"" /*daemon_endpoint*/, "test_segment_name", "" /*sampling_rules*/,
                           "" /*origin*/, aws_metadata_};
  Driver driver(config, context_);

  Tracing::Decision tracing_decision{Tracing::Reason::Sampling, false /*sampled*/};

  auto span = driver.startSpan(tracing_config_, request_headers_, stream_info_, operation_name_,
                               tracing_decision);
  // sampling should fall back to the default manifest since:
  // a) there is no valid sampling decision in the X-Ray header
  // b) there are no sampling rules passed, so the default rules apply (1 req/sec and 5% after that
  // within that second)
  ASSERT_NE(span, nullptr);
}

TEST_F(XRayDriverTest, XRayTraceHeaderWithoutSamplingDecision) {
  request_headers_.set(xRayTraceHeader().key(), "Root=1-272793;Parent=5398ad8;");
  // sampling rules with default fixed_target = 0 & rate = 0
  XRayConfiguration config{"" /*daemon_endpoint*/, "test_segment_name", R"EOF(
{
  "version": 2,
  "default": {
    "fixed_target": 0,
    "rate": 0
  }
}
        )EOF" /*sampling_rules*/,
                           "" /*origin*/, aws_metadata_};
  Driver driver(config, context_);

  Tracing::Decision tracing_decision{Tracing::Reason::Sampling, false /*sampled*/};

  auto span = driver.startSpan(tracing_config_, request_headers_, stream_info_, operation_name_,
                               tracing_decision);
  // sampling will not be done since:
  // a) there is no sampling decision in the X-Ray header
  // b) there is a custom sampling rule passed which still doesn't enforce sampling
  ASSERT_NE(span, nullptr);
  auto* xray_span = static_cast<XRay::Span*>(span.get());
  ASSERT_FALSE(xray_span->sampled());
}

TEST_F(XRayDriverTest, NoXRayTracerHeader) {
  XRayConfiguration config{"" /*daemon_endpoint*/, "test_segment_name", "" /*sampling_rules*/,
                           "" /*origin*/, aws_metadata_};
  Driver driver(config, context_);

  Tracing::Decision tracing_decision{Tracing::Reason::Sampling, false /*sampled*/};

  auto span = driver.startSpan(tracing_config_, request_headers_, stream_info_, operation_name_,
                               tracing_decision);
  // sampling should fall back to the default manifest since:
  // a) there is no X-Ray header to determine the sampling decision
  // b) there are no sampling rules passed, so the default rules apply (1 req/sec and 5% after that
  // within that second)
  ASSERT_NE(span, nullptr);
}

TEST_F(XRayDriverTest, XForwardedForHeaderSet) {
  request_headers_.set(xForwardedForHeader().key(), "191.251.191.251");
  XRayConfiguration config{"" /*daemon_endpoint*/, "test_segment_name", "" /*sampling_rules*/,
                           "" /*origin*/, aws_metadata_};
  Driver driver(config, context_);

  Tracing::Decision tracing_decision{Tracing::Reason::Sampling, false /*sampled*/};

  auto span = driver.startSpan(tracing_config_, request_headers_, stream_info_, operation_name_,
                               tracing_decision);

  ASSERT_NE(span, nullptr);
  auto* xray_span = static_cast<XRay::Span*>(span.get());
  ASSERT_TRUE(xray_span->hasKeyInHttpRequestAnnotations(SpanXForwardedFor));
  ASSERT_TRUE(xray_span->hasKeyInHttpRequestAnnotations(SpanClientIp));
}

TEST_F(XRayDriverTest, XForwardedForHeaderNotSet) {
  XRayConfiguration config{"" /*daemon_endpoint*/, "test_segment_name", "" /*sampling_rules*/,
                           "" /*origin*/, aws_metadata_};
  Driver driver(config, context_);

  Tracing::Decision tracing_decision{Tracing::Reason::Sampling, false /*sampled*/};

  auto span = driver.startSpan(tracing_config_, request_headers_, stream_info_, operation_name_,
                               tracing_decision);

  ASSERT_NE(span, nullptr);
  auto* xray_span = static_cast<XRay::Span*>(span.get());
  ASSERT_FALSE(xray_span->hasKeyInHttpRequestAnnotations(SpanXForwardedFor));
  ASSERT_FALSE(xray_span->hasKeyInHttpRequestAnnotations(SpanClientIp));
}

} // namespace
} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
