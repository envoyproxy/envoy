#include "source/extensions/tracers/opentelemetry/span_context_extractor.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

namespace {

const std::string version{"00"};
const std::string trace_id{"00000000000000000000000000000001"};
const std::string parent_id{"0000000000000003"};
const std::string trace_flags{"01"};

TEST(SpanContextExtractorTest, ExtractSpanContext) {
  Http::TestRequestHeaderMapImpl request_headers{
      {"traceparent", fmt::format("{}-{}-{}-{}", version, trace_id, parent_id, trace_flags)}};
  SpanContextExtractor span_context_extractor(request_headers);
  SpanContext span_context = span_context_extractor.extractSpanContext();

  EXPECT_EQ(span_context.traceId(), trace_id);
  EXPECT_EQ(span_context.parentId(), parent_id);
  EXPECT_EQ(span_context.version(), version);
  EXPECT_TRUE(span_context.sampled());
}

TEST(SpanContextExtractorTest, ExtractSpanContextNotSampled) {
  const std::string trace_flags_unsampled{"00"};
  Http::TestRequestHeaderMapImpl request_headers{
      {"traceparent",
       fmt::format("{}-{}-{}-{}", version, trace_id, parent_id, trace_flags_unsampled)}};
  SpanContextExtractor span_context_extractor(request_headers);
  SpanContext span_context = span_context_extractor.extractSpanContext();

  EXPECT_EQ(span_context.traceId(), trace_id);
  EXPECT_EQ(span_context.parentId(), parent_id);
  EXPECT_EQ(span_context.version(), version);
  EXPECT_FALSE(span_context.sampled());
}

TEST(SpanContextExtractorTest, ThrowsExceptionWithoutHeader) {
  Http::TestRequestHeaderMapImpl request_headers{{}};
  SpanContextExtractor span_context_extractor(request_headers);

  EXPECT_THROW_WITH_MESSAGE(span_context_extractor.extractSpanContext(), ExtractorException,
                            "No propagation header found");
}

TEST(SpanContextExtractorTest, ThrowsExceptionWithTooLongHeader) {
  Http::TestRequestHeaderMapImpl request_headers{
      {"traceparent", fmt::format("000{}-{}-{}-{}", version, trace_id, parent_id, trace_flags)}};
  SpanContextExtractor span_context_extractor(request_headers);

  EXPECT_THROW_WITH_MESSAGE(span_context_extractor.extractSpanContext(), ExtractorException,
                            "Invalid traceparent header length");
}

TEST(SpanContextExtractorTest, ThrowsExceptionWithTooShortHeader) {
  Http::TestRequestHeaderMapImpl request_headers{
      {"traceparent", fmt::format("{}-{}-{}", trace_id, parent_id, trace_flags)}};
  SpanContextExtractor span_context_extractor(request_headers);

  EXPECT_THROW_WITH_MESSAGE(span_context_extractor.extractSpanContext(), ExtractorException,
                            "Invalid traceparent header length");
}

TEST(SpanContextExtractorTest, ThrowsExceptionWithInvalidHyphenation) {
  Http::TestRequestHeaderMapImpl request_headers{
      {"traceparent", fmt::format("{}{}-{}-{}", version, trace_id, parent_id, trace_flags)}};
  SpanContextExtractor span_context_extractor(request_headers);

  EXPECT_THROW_WITH_MESSAGE(span_context_extractor.extractSpanContext(), ExtractorException,
                            "Invalid traceparent header length");
}

TEST(SpanContextExtractorTest, ThrowsExceptionWithInvalidSizes) {
  const std::string invalid_version{"0"};
  const std::string invalid_trace_flags{"001"};
  Http::TestRequestHeaderMapImpl request_headers{
      {"traceparent",
       fmt::format("{}-{}-{}-{}", invalid_version, trace_id, parent_id, invalid_trace_flags)}};
  SpanContextExtractor span_context_extractor(request_headers);

  EXPECT_THROW_WITH_MESSAGE(span_context_extractor.extractSpanContext(), ExtractorException,
                            "Invalid traceparent field sizes");
}

TEST(SpanContextExtractorTest, ThrowsExceptionWithInvalidHex) {
  const std::string invalid_version{"ZZ"};
  Http::TestRequestHeaderMapImpl request_headers{
      {"traceparent",
       fmt::format("{}-{}-{}-{}", invalid_version, trace_id, parent_id, trace_flags)}};
  SpanContextExtractor span_context_extractor(request_headers);

  EXPECT_THROW_WITH_MESSAGE(span_context_extractor.extractSpanContext(), ExtractorException,
                            "Invalid header hex");
}

TEST(SpanContextExtractorTest, ThrowsExceptionWithAllZeroTraceId) {
  const std::string invalid_trace_id{"00000000000000000000000000000000"};
  Http::TestRequestHeaderMapImpl request_headers{
      {"traceparent",
       fmt::format("{}-{}-{}-{}", version, invalid_trace_id, parent_id, trace_flags)}};
  SpanContextExtractor span_context_extractor(request_headers);

  EXPECT_THROW_WITH_MESSAGE(span_context_extractor.extractSpanContext(), ExtractorException,
                            "Invalid trace id");
}

TEST(SpanContextExtractorTest, ThrowsExceptionWithAllZeroParentId) {
  const std::string invalid_parent_id{"0000000000000000"};
  Http::TestRequestHeaderMapImpl request_headers{
      {"traceparent",
       fmt::format("{}-{}-{}-{}", version, trace_id, invalid_parent_id, trace_flags)}};
  SpanContextExtractor span_context_extractor(request_headers);

  EXPECT_THROW_WITH_MESSAGE(span_context_extractor.extractSpanContext(), ExtractorException,
                            "Invalid parent id");
}

} // namespace
} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
