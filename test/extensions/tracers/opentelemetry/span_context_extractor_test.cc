#include <string>

#include "source/common/tracing/http_tracer_impl.h"
#include "source/extensions/tracers/opentelemetry/span_context.h"
#include "source/extensions/tracers/opentelemetry/span_context_extractor.h"

#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

namespace {

using ::Envoy::StatusHelpers::IsOk;
using StatusHelpers::HasStatusMessage;
using ::testing::Not;

constexpr absl::string_view version = "00";
constexpr absl::string_view trace_id = "00000000000000000000000000000001";
constexpr absl::string_view parent_id = "0000000000000003";
constexpr absl::string_view trace_flags = "01";

TEST(SpanContextExtractorTest, ExtractSpanContext) {
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", fmt::format("{}-{}-{}-{}", version, trace_id, parent_id, trace_flags)}};

  SpanContextExtractor span_context_extractor(request_headers);
  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_OK(span_context);
  EXPECT_EQ(span_context->traceId(), trace_id);
  EXPECT_EQ(span_context->spanId(), parent_id);
  EXPECT_EQ(span_context->version(), version);
  EXPECT_TRUE(span_context->sampled());
}

TEST(SpanContextExtractorTest, ExtractSpanContextNotSampled) {
  const std::string trace_flags_unsampled{"00"};
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent",
       fmt::format("{}-{}-{}-{}", version, trace_id, parent_id, trace_flags_unsampled)}};
  SpanContextExtractor span_context_extractor(request_headers);
  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_OK(span_context);
  EXPECT_EQ(span_context->traceId(), trace_id);
  EXPECT_EQ(span_context->spanId(), parent_id);
  EXPECT_EQ(span_context->version(), version);
  EXPECT_FALSE(span_context->sampled());
}

TEST(SpanContextExtractorTest, ThrowsExceptionWithoutHeader) {
  Tracing::TestTraceContextImpl request_headers{{}};
  SpanContextExtractor span_context_extractor(request_headers);

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_THAT(span_context, Not(IsOk()));
  EXPECT_THAT(span_context, HasStatusMessage("No propagation header found"));
}

TEST(SpanContextExtractorTest, ThrowsExceptionWithTooLongHeader) {
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", fmt::format("000{}-{}-{}-{}", version, trace_id, parent_id, trace_flags)}};
  SpanContextExtractor span_context_extractor(request_headers);

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_THAT(span_context, Not(IsOk()));
  EXPECT_THAT(span_context, HasStatusMessage("Invalid traceparent header length"));
}

TEST(SpanContextExtractorTest, ThrowsExceptionWithTooShortHeader) {
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", fmt::format("{}-{}-{}", trace_id, parent_id, trace_flags)}};
  SpanContextExtractor span_context_extractor(request_headers);

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_THAT(span_context, Not(IsOk()));
  EXPECT_THAT(span_context, HasStatusMessage("Invalid traceparent header length"));
}

TEST(SpanContextExtractorTest, ThrowsExceptionWithInvalidHyphenation) {
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", fmt::format("{}{}-{}-{}", version, trace_id, parent_id, trace_flags)}};
  SpanContextExtractor span_context_extractor(request_headers);

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_THAT(span_context, Not(IsOk()));
  EXPECT_THAT(span_context, HasStatusMessage("Invalid traceparent header length"));
}

TEST(SpanContextExtractorTest, ThrowsExceptionWithInvalidSizes) {
  const std::string invalid_version{"0"};
  const std::string invalid_trace_flags{"001"};
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent",
       fmt::format("{}-{}-{}-{}", invalid_version, trace_id, parent_id, invalid_trace_flags)}};
  SpanContextExtractor span_context_extractor(request_headers);

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_THAT(span_context, Not(IsOk()));
  EXPECT_THAT(span_context, HasStatusMessage("Invalid traceparent field sizes"));
}

TEST(SpanContextExtractorTest, ThrowsExceptionWithInvalidHex) {
  const std::string invalid_version{"ZZ"};
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent",
       fmt::format("{}-{}-{}-{}", invalid_version, trace_id, parent_id, trace_flags)}};
  SpanContextExtractor span_context_extractor(request_headers);

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_THAT(span_context, Not(IsOk()));
  EXPECT_THAT(span_context, HasStatusMessage("Invalid header hex"));
}

TEST(SpanContextExtractorTest, ThrowsExceptionWithAllZeroTraceId) {
  const std::string invalid_trace_id{"00000000000000000000000000000000"};
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent",
       fmt::format("{}-{}-{}-{}", version, invalid_trace_id, parent_id, trace_flags)}};
  SpanContextExtractor span_context_extractor(request_headers);

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_THAT(span_context, Not(IsOk()));
  EXPECT_THAT(span_context, HasStatusMessage("Invalid trace id"));
}

TEST(SpanContextExtractorTest, ThrowsExceptionWithAllZeroParentId) {
  const std::string invalid_parent_id{"0000000000000000"};
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent",
       fmt::format("{}-{}-{}-{}", version, trace_id, invalid_parent_id, trace_flags)}};
  SpanContextExtractor span_context_extractor(request_headers);

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_THAT(span_context, Not(IsOk()));
  EXPECT_THAT(span_context, HasStatusMessage("Invalid parent id"));
}

TEST(SpanContextExtractorTest, ExtractSpanContextWithEmptyTracestate) {
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", fmt::format("{}-{}-{}-{}", version, trace_id, parent_id, trace_flags)}};
  SpanContextExtractor span_context_extractor(request_headers);
  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_OK(span_context);
  EXPECT_TRUE(span_context->tracestate().empty());
}

TEST(SpanContextExtractorTest, ExtractSpanContextWithTracestate) {
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", fmt::format("{}-{}-{}-{}", version, trace_id, parent_id, trace_flags)},
      {"tracestate", "sample-tracestate"}};
  SpanContextExtractor span_context_extractor(request_headers);
  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_OK(span_context);
  EXPECT_EQ(span_context->tracestate(), "sample-tracestate");
}

TEST(SpanContextExtractorTest, IgnoreTracestateWithoutTraceparent) {
  Tracing::TestTraceContextImpl request_headers{{"tracestate", "sample-tracestate"}};
  SpanContextExtractor span_context_extractor(request_headers);
  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_THAT(span_context, Not(IsOk()));
  EXPECT_THAT(span_context, HasStatusMessage("No propagation header found"));
}

TEST(SpanContextExtractorTest, ExtractSpanContextWithMultipleTracestateEntries) {
  Http::TestRequestHeaderMapImpl request_headers{
      {"traceparent", fmt::format("{}-{}-{}-{}", version, trace_id, parent_id, trace_flags)},
      {"tracestate", "sample-tracestate"},
      {"tracestate", "sample-tracestate-2"}};
  Tracing::HttpTraceContext trace_context(request_headers);
  SpanContextExtractor span_context_extractor(trace_context);
  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_OK(span_context);
  EXPECT_EQ(span_context->tracestate(), "sample-tracestate,sample-tracestate-2");
}

TEST(SpanContextExtractorTest, SerializeTraceparent) {
  static constexpr absl::string_view kOtherParentId = "feedf00dfeedf00d";
  static constexpr absl::string_view kOtherTraceId = "facefacefacefacefacefacefaceface";

  static_assert(kOtherParentId.size() == parent_id.size());
  static_assert(kOtherTraceId.size() == trace_id.size());

  for (bool sampled : {false, true}) {
    SCOPED_TRACE(testing::Message() << "sampled: " << sampled);

    absl::string_view want_flags = sampled ? "01" : "00";

    SpanContext span_context{version, trace_id, parent_id, sampled,
                             /*tracestate=*/""};
    std::string serialized_traceparent = SpanContextExtractor::serializeTraceparent(span_context);
    EXPECT_EQ(serialized_traceparent,
              fmt::format("{}-{}-{}-{}", version, trace_id, parent_id, want_flags));

    span_context.setSpanId(std::string(kOtherParentId));
    serialized_traceparent = SpanContextExtractor::serializeTraceparent(span_context);
    EXPECT_EQ(serialized_traceparent,
              fmt::format("{}-{}-{}-{}", version, trace_id, kOtherParentId, want_flags));

    span_context.setTraceId(std::string(kOtherTraceId));
    serialized_traceparent = SpanContextExtractor::serializeTraceparent(span_context);
    EXPECT_EQ(serialized_traceparent,
              fmt::format("{}-{}-{}-{}", version, kOtherTraceId, kOtherParentId, want_flags));
  }
}

} // namespace
} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
