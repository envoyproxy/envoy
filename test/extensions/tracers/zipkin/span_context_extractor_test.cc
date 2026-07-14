#include "source/common/tracing/http_tracer_impl.h"
#include "source/extensions/tracers/zipkin/span_context.h"
#include "source/extensions/tracers/zipkin/span_context_extractor.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {
namespace {

const std::string trace_id{"0000000000000001"};
const std::string trace_id_high{"0000000000000009"};
const std::string span_id{"0000000000000003"};
const std::string parent_id{"0000000000000002"};

} // namespace

TEST(ZipkinSpanContextExtractorTest, Largest) {
  Tracing::TestTraceContextImpl request_headers{
      {"b3", fmt::format("{}{}-{}-1-{}", trace_id_high, trace_id, span_id, parent_id)}};
  SpanContextExtractor extractor(request_headers);
  auto context = extractor.extractSpanContext(true);
  EXPECT_TRUE(context.second);
  EXPECT_EQ(3, context.first.id());
  EXPECT_EQ(2, context.first.parentId());
  EXPECT_TRUE(context.first.is128BitTraceId());
  EXPECT_EQ(1, context.first.traceId());
  EXPECT_EQ(9, context.first.traceIdHigh());
  EXPECT_TRUE(context.first.sampled());
  EXPECT_TRUE(extractor.extractSampled().value());
}

TEST(ZipkinSpanContextExtractorTest, WithoutParentDebug) {
  Tracing::TestTraceContextImpl request_headers{
      {"b3", fmt::format("{}{}-{}-d", trace_id_high, trace_id, span_id)}};
  SpanContextExtractor extractor(request_headers);
  auto context = extractor.extractSpanContext(true);
  EXPECT_TRUE(context.second);
  EXPECT_EQ(3, context.first.id());
  EXPECT_EQ(0, context.first.parentId());
  EXPECT_TRUE(context.first.is128BitTraceId());
  EXPECT_EQ(1, context.first.traceId());
  EXPECT_EQ(9, context.first.traceIdHigh());
  EXPECT_TRUE(context.first.sampled());
  EXPECT_TRUE(extractor.extractSampled().value());
}

TEST(ZipkinSpanContextExtractorTest, MalformedUuid) {
  Tracing::TestTraceContextImpl request_headers{{"b3", "b970dafd-0d95-40aa-95d8-1d8725aebe40"}};
  SpanContextExtractor extractor(request_headers);
  EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                            "Invalid input: invalid trace id b970dafd-0d95-40");
  EXPECT_FALSE(extractor.extractSampled().has_value());
}

TEST(ZipkinSpanContextExtractorTest, MiddleOfString) {
  Tracing::TestTraceContextImpl request_headers{
      {"b3", fmt::format("{}{}-{},", trace_id, trace_id, span_id)}};
  SpanContextExtractor extractor(request_headers);
  EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                            "Invalid input: truncated");
  EXPECT_FALSE(extractor.extractSampled().has_value());
}

TEST(ZipkinSpanContextExtractorTest, DebugOnly) {
  Tracing::TestTraceContextImpl request_headers{{"b3", "d"}};
  SpanContextExtractor extractor(request_headers);
  auto context = extractor.extractSpanContext(true);
  EXPECT_FALSE(context.second);
  EXPECT_EQ(0, context.first.id());
  EXPECT_EQ(0, context.first.parentId());
  EXPECT_FALSE(context.first.is128BitTraceId());
  EXPECT_EQ(0, context.first.traceId());
  EXPECT_EQ(0, context.first.traceIdHigh());
  EXPECT_FALSE(context.first.sampled());
  EXPECT_TRUE(extractor.extractSampled().value());
}

TEST(ZipkinSpanContextExtractorTest, Sampled) {
  Tracing::TestTraceContextImpl request_headers{{"b3", "1"}};
  SpanContextExtractor extractor(request_headers);
  auto context = extractor.extractSpanContext(true);
  EXPECT_FALSE(context.second);
  EXPECT_EQ(0, context.first.id());
  EXPECT_EQ(0, context.first.parentId());
  EXPECT_FALSE(context.first.is128BitTraceId());
  EXPECT_EQ(0, context.first.traceId());
  EXPECT_EQ(0, context.first.traceIdHigh());
  EXPECT_FALSE(context.first.sampled());
  EXPECT_TRUE(extractor.extractSampled().value());
}

TEST(ZipkinSpanContextExtractorTest, SampledFalse) {
  Tracing::TestTraceContextImpl request_headers{{"b3", "0"}};
  SpanContextExtractor extractor(request_headers);
  auto context = extractor.extractSpanContext(true);
  EXPECT_FALSE(context.second);
  EXPECT_EQ(0, context.first.id());
  EXPECT_EQ(0, context.first.parentId());
  EXPECT_FALSE(context.first.is128BitTraceId());
  EXPECT_EQ(0, context.first.traceId());
  EXPECT_EQ(0, context.first.traceIdHigh());
  EXPECT_FALSE(context.first.sampled());
  EXPECT_FALSE(extractor.extractSampled().value());
}

TEST(ZipkinSpanContextExtractorTest, IdNotYetSampled128) {
  Tracing::TestTraceContextImpl request_headers{
      {"b3", fmt::format("{}{}-{}", trace_id_high, trace_id, span_id)}};
  SpanContextExtractor extractor(request_headers);
  auto context = extractor.extractSpanContext(true);
  EXPECT_TRUE(context.second);
  EXPECT_EQ(3, context.first.id());
  EXPECT_EQ(0, context.first.parentId());
  EXPECT_TRUE(context.first.is128BitTraceId());
  EXPECT_EQ(1, context.first.traceId());
  EXPECT_EQ(9, context.first.traceIdHigh());
  EXPECT_TRUE(context.first.sampled());
  EXPECT_FALSE(extractor.extractSampled().has_value());
}

TEST(ZipkinSpanContextExtractorTest, IdsUnsampled) {
  Tracing::TestTraceContextImpl request_headers{{"b3", fmt::format("{}-{}-0", trace_id, span_id)}};
  SpanContextExtractor extractor(request_headers);
  auto context = extractor.extractSpanContext(true);
  EXPECT_TRUE(context.second);
  EXPECT_EQ(3, context.first.id());
  EXPECT_EQ(0, context.first.parentId());
  EXPECT_FALSE(context.first.is128BitTraceId());
  EXPECT_EQ(1, context.first.traceId());
  EXPECT_EQ(0, context.first.traceIdHigh());
  EXPECT_TRUE(context.first.sampled());
  EXPECT_FALSE(extractor.extractSampled().value());
}

TEST(ZipkinSpanContextExtractorTest, ParentUnsampled) {
  Tracing::TestTraceContextImpl request_headers{
      {"b3", fmt::format("{}-{}-0-{}", trace_id, span_id, parent_id)}};
  SpanContextExtractor extractor(request_headers);
  auto context = extractor.extractSpanContext(true);
  EXPECT_TRUE(context.second);
  EXPECT_EQ(3, context.first.id());
  EXPECT_EQ(2, context.first.parentId());
  EXPECT_FALSE(context.first.is128BitTraceId());
  EXPECT_EQ(1, context.first.traceId());
  EXPECT_EQ(0, context.first.traceIdHigh());
  EXPECT_TRUE(context.first.sampled());
  EXPECT_FALSE(extractor.extractSampled().value());
}

TEST(ZipkinSpanContextExtractorTest, ParentDebug) {
  Tracing::TestTraceContextImpl request_headers{
      {"b3", fmt::format("{}-{}-d-{}", trace_id, span_id, parent_id)}};
  SpanContextExtractor extractor(request_headers);
  auto context = extractor.extractSpanContext(true);
  EXPECT_TRUE(context.second);
  EXPECT_EQ(3, context.first.id());
  EXPECT_EQ(2, context.first.parentId());
  EXPECT_FALSE(context.first.is128BitTraceId());
  EXPECT_EQ(1, context.first.traceId());
  EXPECT_EQ(0, context.first.traceIdHigh());
  EXPECT_TRUE(context.first.sampled());
  EXPECT_TRUE(extractor.extractSampled().value());
}

TEST(ZipkinSpanContextExtractorTest, IdsWithDebug) {
  Tracing::TestTraceContextImpl request_headers{{"b3", fmt::format("{}-{}-d", trace_id, span_id)}};
  SpanContextExtractor extractor(request_headers);
  auto context = extractor.extractSpanContext(true);
  EXPECT_TRUE(context.second);
  EXPECT_EQ(3, context.first.id());
  EXPECT_EQ(0, context.first.parentId());
  EXPECT_FALSE(context.first.is128BitTraceId());
  EXPECT_EQ(1, context.first.traceId());
  EXPECT_EQ(0, context.first.traceIdHigh());
  EXPECT_TRUE(context.first.sampled());
  EXPECT_TRUE(extractor.extractSampled().value());
}

TEST(ZipkinSpanContextExtractorTest, WithoutSampled) {
  Tracing::TestTraceContextImpl request_headers{{"b3", fmt::format("{}-{}", trace_id, span_id)}};
  SpanContextExtractor extractor(request_headers);
  auto context = extractor.extractSpanContext(false);
  EXPECT_TRUE(context.second);
  EXPECT_EQ(3, context.first.id());
  EXPECT_EQ(0, context.first.parentId());
  EXPECT_FALSE(context.first.is128BitTraceId());
  EXPECT_EQ(1, context.first.traceId());
  EXPECT_EQ(0, context.first.traceIdHigh());
  EXPECT_FALSE(context.first.sampled());
  EXPECT_FALSE(extractor.extractSampled().has_value());
}

TEST(ZipkinSpanContextExtractorTest, TooBig) {
  {
    Tracing::TestTraceContextImpl request_headers{
        {"b3", fmt::format("{}{}{}-{}-{}", trace_id, trace_id, trace_id, span_id, trace_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: too long");
    EXPECT_FALSE(extractor.extractSampled().has_value());
  }

  {
    Tracing::TestTraceContextImpl request_headers{
        {"b3", fmt::format("{}{}-{}-1-{}a", trace_id_high, trace_id, span_id, parent_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: too long");
  }
}

TEST(ZipkinSpanContextExtractorTest, Empty) {
  Tracing::TestTraceContextImpl request_headers{{"b3", ""}};
  SpanContextExtractor extractor(request_headers);
  EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                            "Invalid input: empty");
}

TEST(ZipkinSpanContextExtractorTest, InvalidInput) {
  {
    Tracing::TestTraceContextImpl request_headers{
        {"x-b3-traceid", trace_id_high + trace_id.substr(0, 15) + "!"}, {"x-b3-spanid", span_id}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              fmt::format("Invalid traceid_high {} or tracid {}", trace_id_high,
                                          trace_id.substr(0, 15) + "!"));
  }

  {
    Tracing::TestTraceContextImpl request_headers{
        {"b3", fmt::format("{}!{}-{}", trace_id.substr(0, 15), trace_id, span_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(
        extractor.extractSpanContext(true), ExtractorException,
        fmt::format("Invalid input: invalid trace id high {}!", trace_id.substr(0, 15)));
  }

  {
    Tracing::TestTraceContextImpl request_headers{
        {"b3", fmt::format("{}{}!-{}", trace_id, trace_id.substr(0, 15), span_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(
        extractor.extractSpanContext(true), ExtractorException,
        fmt::format("Invalid input: invalid trace id {}!", trace_id.substr(0, 15)));
  }

  {
    Tracing::TestTraceContextImpl request_headers{
        {"b3", fmt::format("{}!-{}", trace_id.substr(0, 15), span_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(
        extractor.extractSpanContext(true), ExtractorException,
        fmt::format("Invalid input: invalid trace id {}!", trace_id.substr(0, 15)));
  }

  {
    Tracing::TestTraceContextImpl request_headers{{"b3", fmt::format("{}!{}", trace_id, span_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: not exists span id");
  }

  {
    Tracing::TestTraceContextImpl request_headers{
        {"b3", fmt::format("{}-{}!", trace_id, span_id.substr(0, 15))}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(
        extractor.extractSpanContext(true), ExtractorException,
        fmt::format("Invalid input: invalid span id {}!", span_id.substr(0, 15)));
  }

  {
    Tracing::TestTraceContextImpl request_headers{
        {"b3", fmt::format("{}-{}!0", trace_id, span_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: not exists sampling field");
  }

  {
    Tracing::TestTraceContextImpl request_headers{
        {"b3", fmt::format("{}-{}-c", trace_id, span_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: invalid sampling flag c");
  }

  {
    Tracing::TestTraceContextImpl request_headers{
        {"b3", fmt::format("{}-{}-d!{}", trace_id, span_id, parent_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }

  {
    Tracing::TestTraceContextImpl request_headers{
        {"b3", fmt::format("{}-{}-d-{}!", trace_id, span_id, parent_id.substr(0, 15))}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(
        extractor.extractSpanContext(true), ExtractorException,
        fmt::format("Invalid input: invalid parent id {}!", parent_id.substr(0, 15)));
  }

  {
    Tracing::TestTraceContextImpl request_headers{{"b3", "-"}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_FALSE(extractor.extractSampled().has_value());
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: invalid sampling flag -");
  }
}

TEST(ZipkinSpanContextExtractorTest, Truncated) {
  {
    Tracing::TestTraceContextImpl request_headers{{"b3", "-1"}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }

  {
    Tracing::TestTraceContextImpl request_headers{{"b3", "1-"}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }

  {
    Tracing::TestTraceContextImpl request_headers{{"b3", "1-"}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }

  {
    Tracing::TestTraceContextImpl request_headers{{"b3", trace_id.substr(0, 15)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }

  {
    Tracing::TestTraceContextImpl request_headers{{"b3", trace_id}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }

  {
    Tracing::TestTraceContextImpl request_headers{{"b3", trace_id + "-"}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }

  {
    Tracing::TestTraceContextImpl request_headers{
        {"b3", fmt::format("{}-{}", trace_id.substr(0, 15), span_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }

  {
    Tracing::TestTraceContextImpl request_headers{
        {"b3", fmt::format("{}-{}", trace_id, span_id.substr(0, 15))}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }

  {
    Tracing::TestTraceContextImpl request_headers{{"b3", fmt::format("{}-{}-", trace_id, span_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }

  {
    Tracing::TestTraceContextImpl request_headers{
        {"b3", fmt::format("{}-{}-1-", trace_id, span_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }

  {
    Tracing::TestTraceContextImpl request_headers{
        {"b3", fmt::format("{}-{}-1-{}", trace_id, span_id, parent_id.substr(0, 15))}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }

  {
    Tracing::TestTraceContextImpl request_headers{
        {"b3", fmt::format("{}-{}-{}{}", trace_id, span_id, trace_id, trace_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }
}

// Test W3C fallback functionality
TEST(ZipkinSpanContextExtractorTest, W3CFallbackDisabledByDefault) {
  // Test that W3C headers are ignored when w3c_fallback is disabled (default)
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01"}};
  SpanContextExtractor extractor(request_headers); // w3c_fallback disabled by default
  auto context = extractor.extractSpanContext(true);
  EXPECT_FALSE(context.second); // Should not extract context from W3C headers
  EXPECT_FALSE(extractor.extractSampled().has_value());
}

TEST(ZipkinSpanContextExtractorTest, W3CFallbackEnabled) {
  // Test that W3C headers are used when w3c_fallback is enabled
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01"}};
  SpanContextExtractor extractor(request_headers, true); // w3c_fallback enabled
  auto context = extractor.extractSpanContext(true);
  EXPECT_TRUE(context.second); // Should extract context from W3C headers
  EXPECT_TRUE(extractor.extractSampled().value());

  // Verify the converted values
  EXPECT_EQ(0xb7ad6b7169203331, context.first.id()); // W3C span-id becomes span-id
  EXPECT_EQ(0, context.first.parentId());            // No parent in W3C conversion
  EXPECT_TRUE(context.first.is128BitTraceId());
  EXPECT_EQ(0x8448eb211c80319c, context.first.traceId());     // Low 64 bits
  EXPECT_EQ(0x0af7651916cd43dd, context.first.traceIdHigh()); // High 64 bits
}

TEST(ZipkinSpanContextExtractorTest, B3TakesPrecedenceOverW3C) {
  // Test that B3 headers take precedence over W3C headers when both are present
  Tracing::TestTraceContextImpl request_headers{
      {"b3", fmt::format("{}-{}-1", trace_id, span_id)},
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01"}};
  SpanContextExtractor extractor(request_headers, true); // w3c_fallback enabled
  auto context = extractor.extractSpanContext(true);
  EXPECT_TRUE(context.second);

  // Should use B3 values, not W3C values
  EXPECT_EQ(3, context.first.id()); // From B3 span_id
  EXPECT_EQ(0, context.first.parentId());
  EXPECT_FALSE(context.first.is128BitTraceId()); // B3 uses 64-bit in this test
  EXPECT_EQ(1, context.first.traceId());         // From B3 trace_id
}

TEST(ZipkinSpanContextExtractorTest, W3CFallbackWithInvalidHeaders) {
  // Test that invalid W3C headers are handled gracefully
  Tracing::TestTraceContextImpl request_headers{{"traceparent", "invalid-header-format"}};
  SpanContextExtractor extractor(request_headers, true); // w3c_fallback enabled
  auto context = extractor.extractSpanContext(true);
  EXPECT_FALSE(context.second); // Should not extract context from invalid W3C headers
  EXPECT_FALSE(extractor.extractSampled().has_value());
}

TEST(ZipkinSpanContextExtractorTest, W3CFallbackWithInvalidTraceIdLength) {
  // Test invalid W3C trace ID length (too short)
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319-b7ad6b7169203331-01"}};
  SpanContextExtractor extractor(request_headers, true);
  auto context = extractor.extractSpanContext(true);
  EXPECT_FALSE(context.second);

  // Test invalid W3C trace ID length (too long)
  Tracing::TestTraceContextImpl request_headers2{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319c123-b7ad6b7169203331-01"}};
  SpanContextExtractor extractor2(request_headers2, true);
  auto context2 = extractor2.extractSpanContext(true);
  EXPECT_FALSE(context2.second);
}

TEST(ZipkinSpanContextExtractorTest, W3CTraceIdLengthValidation) {
  // Test that invalid W3C trace ID lengths are properly rejected
  // Invalid headers should not extract a valid context (context.second should be false)

  // Too short trace ID (31 chars instead of 32)
  Tracing::TestTraceContextImpl request_headers1{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319-b7ad6b7169203331-01"}};
  SpanContextExtractor extractor1(request_headers1, true);
  auto context1 = extractor1.extractSpanContext(true);
  EXPECT_FALSE(context1.second); // Should not extract context from invalid trace ID length

  // Too long trace ID (33 chars instead of 32)
  Tracing::TestTraceContextImpl request_headers2{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319c1-b7ad6b7169203331-01"}};
  SpanContextExtractor extractor2(request_headers2, true);
  auto context2 = extractor2.extractSpanContext(true);
  EXPECT_FALSE(context2.second); // Should not extract context from invalid trace ID length

  // Empty trace ID
  Tracing::TestTraceContextImpl request_headers3{{"traceparent", "00--b7ad6b7169203331-01"}};
  SpanContextExtractor extractor3(request_headers3, true);
  auto context3 = extractor3.extractSpanContext(true);
  EXPECT_FALSE(context3.second); // Should not extract context from empty trace ID
}

TEST(ZipkinSpanContextExtractorTest, W3CFallbackWithInvalidSpanIdLength) {
  // Test invalid W3C span ID length (too short)
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319c-b7ad6b716920331-01"}};
  SpanContextExtractor extractor(request_headers, true);
  auto context = extractor.extractSpanContext(true);
  EXPECT_FALSE(context.second);

  // Test invalid W3C span ID length (too long)
  Tracing::TestTraceContextImpl request_headers2{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331123-01"}};
  SpanContextExtractor extractor2(request_headers2, true);
  auto context2 = extractor2.extractSpanContext(true);
  EXPECT_FALSE(context2.second);
}

TEST(ZipkinSpanContextExtractorTest, W3CFallbackWithInvalidHexCharacters) {
  // Test invalid hex characters in trace ID
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319g-b7ad6b7169203331-01"}};
  SpanContextExtractor extractor(request_headers, true);
  auto context = extractor.extractSpanContext(true);
  EXPECT_FALSE(context.second);

  // Test invalid hex characters in span ID
  Tracing::TestTraceContextImpl request_headers2{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331g-01"}};
  SpanContextExtractor extractor2(request_headers2, true);
  auto context2 = extractor2.extractSpanContext(true);
  EXPECT_FALSE(context2.second);
}

TEST(ZipkinSpanContextExtractorTest, W3CTraceIdHexValidation) {
  // Test that invalid hex characters in W3C trace IDs are properly rejected
  // Invalid headers should not extract a valid context (context.second should be false)

  // Invalid hex character 'g' in high part of trace ID
  Tracing::TestTraceContextImpl request_headers1{
      {"traceparent", "00-0af7651916cd43dg8448eb211c80319c-b7ad6b7169203331-01"}};
  SpanContextExtractor extractor1(request_headers1, true);
  auto context1 = extractor1.extractSpanContext(true);
  EXPECT_FALSE(context1.second); // Should not extract context from invalid hex in trace ID

  // Invalid hex character 'z' in low part of trace ID
  Tracing::TestTraceContextImpl request_headers2{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319z-b7ad6b7169203331-01"}};
  SpanContextExtractor extractor2(request_headers2, true);
  auto context2 = extractor2.extractSpanContext(true);
  EXPECT_FALSE(context2.second); // Should not extract context from invalid hex in trace ID

  // Invalid character at start of trace ID
  Tracing::TestTraceContextImpl request_headers3{
      {"traceparent", "00-xaf7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01"}};
  SpanContextExtractor extractor3(request_headers3, true);
  auto context3 = extractor3.extractSpanContext(true);
  EXPECT_FALSE(context3.second); // Should not extract context from invalid hex in trace ID

  // Invalid character at end of trace ID
  Tracing::TestTraceContextImpl request_headers4{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319x-b7ad6b7169203331-01"}};
  SpanContextExtractor extractor4(request_headers4, true);
  auto context4 = extractor4.extractSpanContext(true);
  EXPECT_FALSE(context4.second); // Should not extract context from invalid hex in trace ID
}

TEST(ZipkinSpanContextExtractorTest, W3CSpanIdHexValidation) {
  // Test that invalid hex characters in W3C span IDs are properly rejected
  // Invalid headers should not extract a valid context (context.second should be false)

  // Invalid hex character 'g' in span ID
  Tracing::TestTraceContextImpl request_headers1{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331g-01"}};
  SpanContextExtractor extractor1(request_headers1, true);
  auto context1 = extractor1.extractSpanContext(true);
  EXPECT_FALSE(context1.second); // Should not extract context from invalid hex in span ID

  // Invalid hex character 'z' in span ID
  Tracing::TestTraceContextImpl request_headers2{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331z-01"}};
  SpanContextExtractor extractor2(request_headers2, true);
  auto context2 = extractor2.extractSpanContext(true);
  EXPECT_FALSE(context2.second); // Should not extract context from invalid hex in span ID

  // Invalid character at start of span ID
  Tracing::TestTraceContextImpl request_headers3{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319c-x7ad6b7169203331-01"}};
  SpanContextExtractor extractor3(request_headers3, true);
  auto context3 = extractor3.extractSpanContext(true);
  EXPECT_FALSE(context3.second); // Should not extract context from invalid hex in span ID

  // Invalid character in middle of span ID
  Tracing::TestTraceContextImpl request_headers4{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319c-b7ad6b71692x3331-01"}};
  SpanContextExtractor extractor4(request_headers4, true);
  auto context4 = extractor4.extractSpanContext(true);
  EXPECT_FALSE(context4.second); // Should not extract context from invalid hex in span ID

  // Non-hex character like space
  Tracing::TestTraceContextImpl request_headers5{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203 31-01"}};
  SpanContextExtractor extractor5(request_headers5, true);
  auto context5 = extractor5.extractSpanContext(true);
  EXPECT_FALSE(context5.second); // Should not extract context from invalid hex in span ID
}

TEST(ZipkinSpanContextExtractorTest, W3CFallbackWithMalformedTraceparent) {
  // Test missing components
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319c"}};
  SpanContextExtractor extractor(request_headers, true);
  auto context = extractor.extractSpanContext(true);
  EXPECT_FALSE(context.second);

  // Test wrong number of dashes
  Tracing::TestTraceContextImpl request_headers2{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01-extra"}};
  SpanContextExtractor extractor2(request_headers2, true);
  auto context2 = extractor2.extractSpanContext(true);
  EXPECT_FALSE(context2.second);

  // Test empty traceparent
  Tracing::TestTraceContextImpl request_headers3{{"traceparent", ""}};
  SpanContextExtractor extractor3(request_headers3, true);
  auto context3 = extractor3.extractSpanContext(true);
  EXPECT_FALSE(context3.second);
}

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
