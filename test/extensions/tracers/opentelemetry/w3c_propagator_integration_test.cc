#include "source/extensions/propagators/w3c/propagator.h"
#include "source/extensions/tracers/opentelemetry/span_context_extractor.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {
namespace {

// Test that the W3C propagator integration works with OpenTelemetry tracer
class OpenTelemetryW3CPropagatorIntegrationTest : public testing::Test {
protected:
  void SetUp() override {
    // Common W3C trace context values for testing
    version_ = "00";
    trace_id_ = "0af7651916cd43dd8448eb211c80319c";
    span_id_ = "b7ad6b7169203331";
    trace_flags_ = "01";
    traceparent_value_ = fmt::format("{}-{}-{}-{}", version_, trace_id_, span_id_, trace_flags_);
  }

  std::string version_;
  std::string trace_id_;
  std::string span_id_;
  std::string trace_flags_;
  std::string traceparent_value_;
};

// Test that OpenTelemetry span context extractor works with W3C propagator
TEST_F(OpenTelemetryW3CPropagatorIntegrationTest, ExtractorWithW3CPropagator) {
  Tracing::TestTraceContextImpl request_headers{{"traceparent", traceparent_value_}};

  // Test that W3C propagator can extract the context
  auto w3c_result = Propagators::W3C::Propagator::extract(request_headers);
  EXPECT_TRUE(w3c_result.ok()) << w3c_result.status().message();

  if (w3c_result.ok()) {
    const auto& w3c_context = w3c_result.value();
    EXPECT_EQ(w3c_context.traceParent().traceId(), trace_id_);
    EXPECT_EQ(w3c_context.traceParent().parentId(), span_id_);
    EXPECT_TRUE(w3c_context.traceParent().isSampled());
  }

  // Test that OpenTelemetry extractor works with the same headers
  SpanContextExtractor otel_extractor(request_headers);
  EXPECT_TRUE(otel_extractor.propagationHeaderPresent());

  auto otel_result = otel_extractor.extractSpanContext();
  EXPECT_TRUE(otel_result.ok()) << otel_result.status().message();

  if (otel_result.ok()) {
    const auto& otel_context = otel_result.value();
    EXPECT_EQ(otel_context.traceId(), trace_id_);
    EXPECT_EQ(otel_context.spanId(), span_id_);
    EXPECT_TRUE(otel_context.sampled());
  }
}

// Test that W3C headers can be injected and then extracted by OpenTelemetry
TEST_F(OpenTelemetryW3CPropagatorIntegrationTest, W3CRoundTripCompatibility) {
  // Create a W3C trace context
  auto w3c_context_result = Propagators::W3C::Propagator::createRoot(trace_id_, span_id_, true);
  EXPECT_TRUE(w3c_context_result.ok()) << w3c_context_result.status().message();

  if (!w3c_context_result.ok()) {
    return;
  }

  // Inject the context into headers
  Tracing::TestTraceContextImpl headers{};
  Propagators::W3C::Propagator::inject(w3c_context_result.value(), headers);

  // Verify the headers were set correctly
  auto traceparent_header = headers.get("traceparent");
  EXPECT_TRUE(traceparent_header.has_value());
  EXPECT_EQ(traceparent_header.value(), traceparent_value_);

  // Test that OpenTelemetry can extract from the injected headers
  SpanContextExtractor otel_extractor(headers);
  auto otel_result = otel_extractor.extractSpanContext();
  EXPECT_TRUE(otel_result.ok());

  if (otel_result.ok()) {
    EXPECT_EQ(otel_result.value().traceId(), trace_id_);
    EXPECT_EQ(otel_result.value().spanId(), span_id_);
    EXPECT_TRUE(otel_result.value().sampled());
  }
}

// Test that OpenTelemetry properly handles missing W3C headers
TEST_F(OpenTelemetryW3CPropagatorIntegrationTest, MissingW3CHeaders) {
  Tracing::TestTraceContextImpl empty_headers{};

  // Test that W3C propagator correctly reports missing headers
  EXPECT_FALSE(Propagators::W3C::Propagator::isPresent(empty_headers));

  auto w3c_result = Propagators::W3C::Propagator::extract(empty_headers);
  EXPECT_FALSE(w3c_result.ok());

  // Test that OpenTelemetry extractor correctly reports missing headers
  SpanContextExtractor otel_extractor(empty_headers);
  EXPECT_FALSE(otel_extractor.propagationHeaderPresent());

  auto otel_result = otel_extractor.extractSpanContext();
  EXPECT_FALSE(otel_result.ok());
}

// Test that OpenTelemetry properly handles invalid W3C headers
TEST_F(OpenTelemetryW3CPropagatorIntegrationTest, InvalidW3CHeaders) {
  Tracing::TestTraceContextImpl invalid_headers{{"traceparent", "invalid-header-format"}};

  // Test that W3C propagator correctly rejects invalid headers
  auto w3c_result = Propagators::W3C::Propagator::extract(invalid_headers);
  EXPECT_FALSE(w3c_result.ok());

  // Test that OpenTelemetry extractor correctly rejects invalid headers
  SpanContextExtractor otel_extractor(invalid_headers);
  EXPECT_TRUE(otel_extractor.propagationHeaderPresent()); // Header is present but invalid

  auto otel_result = otel_extractor.extractSpanContext();
  EXPECT_FALSE(otel_result.ok());
}

// Test W3C tracestate header handling
TEST_F(OpenTelemetryW3CPropagatorIntegrationTest, TracestateHandling) {
  std::string tracestate_value = "vendor1=value1,vendor2=value2";
  Tracing::TestTraceContextImpl request_headers{{"traceparent", traceparent_value_},
                                                {"tracestate", tracestate_value}};

  // Test that W3C propagator can extract tracestate
  auto w3c_result = Propagators::W3C::Propagator::extract(request_headers);
  EXPECT_TRUE(w3c_result.ok());

  if (w3c_result.ok()) {
    const auto& w3c_context = w3c_result.value();
    // Check that tracestate is present
    EXPECT_FALSE(w3c_context.traceState().toString().empty());
  }

  // Test that OpenTelemetry extractor preserves tracestate
  SpanContextExtractor otel_extractor(request_headers);
  auto otel_result = otel_extractor.extractSpanContext();
  EXPECT_TRUE(otel_result.ok());

  if (otel_result.ok()) {
    const auto& otel_context = otel_result.value();
    EXPECT_FALSE(otel_context.tracestate().empty());
  }
}

} // namespace
} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
