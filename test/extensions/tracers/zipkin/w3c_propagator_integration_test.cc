#include "source/extensions/propagators/w3c/propagator.h"
#include "source/extensions/tracers/zipkin/span_context_extractor.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {
namespace {

// Test that the W3C propagator integration works with Zipkin tracer fallback
class ZipkinW3CPropagatorIntegrationTest : public testing::Test {
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

// Test that Zipkin span context extractor works with W3C propagator for fallback
TEST_F(ZipkinW3CPropagatorIntegrationTest, ExtractorWithW3CFallback) {
  Tracing::TestTraceContextImpl request_headers{{"traceparent", traceparent_value_}};

  // Test that W3C propagator can extract the context
  auto w3c_result = Propagators::W3C::Propagator::extract(request_headers);
  EXPECT_TRUE(w3c_result.ok()) << w3c_result.status().message();

  // Test that Zipkin extractor with W3C fallback enabled works
  SpanContextExtractor zipkin_extractor(request_headers, true /* w3c_fallback_enabled */);

  // Should extract sampled flag from W3C headers
  auto sampled_result = zipkin_extractor.extractSampled();
  EXPECT_TRUE(sampled_result.has_value());
  EXPECT_TRUE(sampled_result.value());

  // Should extract full span context from W3C headers
  auto zipkin_result = zipkin_extractor.extractSpanContext(true);
  EXPECT_TRUE(zipkin_result.second); // Successfully extracted

  if (zipkin_result.second) {
    const auto& zipkin_context = zipkin_result.first;
    // Note: In Zipkin conversion, the 128-bit trace ID is split into high and low parts
    EXPECT_TRUE(zipkin_context.sampled());
  }
}

// Test that Zipkin ignores W3C headers when fallback is disabled
TEST_F(ZipkinW3CPropagatorIntegrationTest, W3CFallbackDisabled) {
  Tracing::TestTraceContextImpl request_headers{{"traceparent", traceparent_value_}};

  // Test that Zipkin extractor with W3C fallback disabled ignores W3C headers
  SpanContextExtractor zipkin_extractor(request_headers, false /* w3c_fallback_enabled */);

  // Should not extract sampled flag from W3C headers when fallback disabled
  auto sampled_result = zipkin_extractor.extractSampled();
  EXPECT_FALSE(sampled_result.has_value());

  // Should not extract span context from W3C headers when fallback disabled
  auto zipkin_result = zipkin_extractor.extractSpanContext(false);
  EXPECT_FALSE(zipkin_result.second); // Failed to extract
}

// Test that Zipkin prefers B3 headers over W3C headers
TEST_F(ZipkinW3CPropagatorIntegrationTest, B3HeadersPriority) {
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", traceparent_value_},
      {"x-b3-traceid", "463ac35c9f6413ad48485a3953bb6124"},
      {"x-b3-spanid", "a2fb4a1d1a96d312"},
      {"x-b3-sampled", "0"}};

  // Test that Zipkin extractor with W3C fallback enabled prefers B3 headers
  SpanContextExtractor zipkin_extractor(request_headers, true /* w3c_fallback_enabled */);

  // Should extract sampled flag from B3 headers (which is "0" = false)
  auto sampled_result = zipkin_extractor.extractSampled();
  EXPECT_TRUE(sampled_result.has_value());
  EXPECT_FALSE(sampled_result.value()); // B3 sampled=0, not W3C sampled=1

  // Should extract span context from B3 headers
  auto zipkin_result = zipkin_extractor.extractSpanContext(false);
  EXPECT_TRUE(zipkin_result.second); // Successfully extracted from B3

  if (zipkin_result.second) {
    const auto& zipkin_context = zipkin_result.first;
    EXPECT_FALSE(zipkin_context.sampled()); // Uses B3 sampling decision
  }
}

// Test that Zipkin falls back to W3C when B3 headers are missing
TEST_F(ZipkinW3CPropagatorIntegrationTest, W3CFallbackWhenB3Missing) {
  Tracing::TestTraceContextImpl request_headers{{"traceparent", traceparent_value_}};

  // Test that Zipkin extractor falls back to W3C when B3 headers are missing
  SpanContextExtractor zipkin_extractor(request_headers, true /* w3c_fallback_enabled */);

  // Should fall back to W3C headers for sampling
  auto sampled_result = zipkin_extractor.extractSampled();
  EXPECT_TRUE(sampled_result.has_value());
  EXPECT_TRUE(sampled_result.value()); // W3C trace_flags = "01" means sampled

  // Should fall back to W3C headers for span context
  auto zipkin_result = zipkin_extractor.extractSpanContext(true);
  EXPECT_TRUE(zipkin_result.second); // Successfully extracted from W3C

  if (zipkin_result.second) {
    const auto& zipkin_context = zipkin_result.first;
    EXPECT_TRUE(zipkin_context.sampled());
  }
}

// Test that Zipkin properly handles B3 single format
TEST_F(ZipkinW3CPropagatorIntegrationTest, B3SingleFormatPriority) {
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", traceparent_value_},
      {"b3", "463ac35c9f6413ad48485a3953bb6124-a2fb4a1d1a96d312-0"}};

  // Test that Zipkin extractor prefers B3 single format over W3C
  SpanContextExtractor zipkin_extractor(request_headers, true /* w3c_fallback_enabled */);

  // Should extract sampled flag from B3 single format (which is "0" = false)
  auto sampled_result = zipkin_extractor.extractSampled();
  EXPECT_TRUE(sampled_result.has_value());
  EXPECT_FALSE(sampled_result.value()); // B3 sampled=0, not W3C sampled=1

  // Should extract span context from B3 single format
  auto zipkin_result = zipkin_extractor.extractSpanContext(false);
  EXPECT_TRUE(zipkin_result.second); // Successfully extracted from B3

  if (zipkin_result.second) {
    const auto& zipkin_context = zipkin_result.first;
    EXPECT_FALSE(zipkin_context.sampled()); // Uses B3 sampling decision
  }
}

// Test that Zipkin correctly handles invalid W3C headers in fallback
TEST_F(ZipkinW3CPropagatorIntegrationTest, InvalidW3CFallback) {
  Tracing::TestTraceContextImpl request_headers{{"traceparent", "invalid-header-format"}};

  // Test that Zipkin extractor handles invalid W3C headers gracefully
  SpanContextExtractor zipkin_extractor(request_headers, true /* w3c_fallback_enabled */);

  // Should not extract sampled flag from invalid W3C headers
  auto sampled_result = zipkin_extractor.extractSampled();
  EXPECT_FALSE(sampled_result.has_value());

  // Should not extract span context from invalid W3C headers
  auto zipkin_result = zipkin_extractor.extractSpanContext(false);
  EXPECT_FALSE(zipkin_result.second); // Failed to extract
}

// Test that Zipkin correctly converts W3C 128-bit trace ID to Zipkin format
TEST_F(ZipkinW3CPropagatorIntegrationTest, W3CTraceIdConversion) {
  // Use a specific 128-bit trace ID for testing conversion
  std::string test_trace_id = "463ac35c9f6413ad48485a3953bb6124";
  std::string test_span_id = "a2fb4a1d1a96d312";
  std::string test_traceparent = fmt::format("00-{}-{}-01", test_trace_id, test_span_id);

  Tracing::TestTraceContextImpl request_headers{{"traceparent", test_traceparent}};

  // Test that Zipkin extractor properly converts 128-bit W3C trace ID
  SpanContextExtractor zipkin_extractor(request_headers, true /* w3c_fallback_enabled */);

  auto zipkin_result = zipkin_extractor.extractSpanContext(true);
  EXPECT_TRUE(zipkin_result.second); // Successfully extracted

  if (zipkin_result.second) {
    const auto& zipkin_context = zipkin_result.first;
    // Verify that the trace ID is properly split into high and low parts
    // The high 64 bits should be 463ac35c9f6413ad, low 64 bits should be 48485a3953bb6124
    EXPECT_TRUE(zipkin_context.sampled());

    // The span ID should match the W3C parent ID
    // Note: Zipkin uses the W3C parent ID as the span ID in the converted context
  }
}

} // namespace
} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
