#include "source/common/tracing/http_tracer_impl.h"
#include "source/extensions/tracers/opentelemetry/span_context_extractor.h"
#include "source/extensions/propagators/opentelemetry/propagator_factory.h"

#include "test/mocks/api/mocks.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

namespace {

using StatusHelpers::HasStatusMessage;
using testing::NiceMock;

constexpr absl::string_view version = "00";
constexpr absl::string_view trace_id = "00000000000000000000000000000001";
constexpr absl::string_view parent_id = "0000000000000003";
constexpr absl::string_view trace_flags = "01";

class SpanContextExtractorTest : public testing::Test {
public:
  SpanContextExtractorTest() = default;

protected:
  // Helper to create a default propagator for testing
  Propagators::OpenTelemetry::CompositePropagatorPtr createDefaultPropagator() {
    std::vector<std::string> propagator_names = {"tracecontext"};
    return Propagators::OpenTelemetry::PropagatorFactory::createPropagators(propagator_names, api_);
  }

  NiceMock<Api::MockApi> api_;
};

TEST_F(SpanContextExtractorTest, ExtractSpanContext) {
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", fmt::format("{}-{}-{}-{}", version, trace_id, parent_id, trace_flags)}};

  auto propagator = createDefaultPropagator();
  SpanContextExtractor span_context_extractor(request_headers, std::move(propagator));
  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_OK(span_context);
  EXPECT_EQ(span_context->traceId(), trace_id);
  EXPECT_EQ(span_context->spanId(), parent_id);
  EXPECT_EQ(span_context->version(), version);
  EXPECT_TRUE(span_context->sampled());
}

TEST_F(SpanContextExtractorTest, ExtractSpanContextNotSampled) {
  const std::string trace_flags_unsampled{"00"};
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent",
       fmt::format("{}-{}-{}-{}", version, trace_id, parent_id, trace_flags_unsampled)}};
  auto propagator = createDefaultPropagator();
  SpanContextExtractor span_context_extractor(request_headers, std::move(propagator));
  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_OK(span_context);
  EXPECT_EQ(span_context->traceId(), trace_id);
  EXPECT_EQ(span_context->spanId(), parent_id);
  EXPECT_EQ(span_context->version(), version);
  EXPECT_FALSE(span_context->sampled());
}

TEST_F(SpanContextExtractorTest, ThrowsExceptionWithoutHeader) {
  Tracing::TestTraceContextImpl request_headers{{}};
  auto propagator = createDefaultPropagator();
  SpanContextExtractor span_context_extractor(request_headers, std::move(propagator));

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_FALSE(span_context.ok());
  EXPECT_THAT(span_context, HasStatusMessage("No propagator could extract span context"));
}

TEST_F(SpanContextExtractorTest, ThrowsExceptionWithTooLongHeader) {
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", fmt::format("000{}-{}-{}-{}", version, trace_id, parent_id, trace_flags)}};
  auto propagator = createDefaultPropagator();
  SpanContextExtractor span_context_extractor(request_headers, std::move(propagator));

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_FALSE(span_context.ok());
  EXPECT_THAT(span_context, HasStatusMessage("Invalid traceparent header length"));
}

TEST_F(SpanContextExtractorTest, ThrowsExceptionWithTooShortHeader) {
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", fmt::format("{}-{}-{}", trace_id, parent_id, trace_flags)}};
  auto propagator = createDefaultPropagator();
  SpanContextExtractor span_context_extractor(request_headers, std::move(propagator));

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_FALSE(span_context.ok());
  EXPECT_THAT(span_context, HasStatusMessage("Invalid traceparent header length"));
}

TEST_F(SpanContextExtractorTest, ThrowsExceptionWithInvalidHyphenation) {
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", fmt::format("{}{}-{}-{}", version, trace_id, parent_id, trace_flags)}};
  auto propagator = createDefaultPropagator();
  SpanContextExtractor span_context_extractor(request_headers, std::move(propagator));

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_FALSE(span_context.ok());
  EXPECT_THAT(span_context, HasStatusMessage("Invalid traceparent header length"));
}

TEST_F(SpanContextExtractorTest, ThrowsExceptionWithInvalidSizes) {
  const std::string invalid_version{"0"};
  const std::string invalid_trace_flags{"001"};
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent",
       fmt::format("{}-{}-{}-{}", invalid_version, trace_id, parent_id, invalid_trace_flags)}};
  auto propagator = createDefaultPropagator();
  SpanContextExtractor span_context_extractor(request_headers, std::move(propagator));

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_FALSE(span_context.ok());
  EXPECT_THAT(span_context, HasStatusMessage("Invalid traceparent field sizes"));
}

TEST_F(SpanContextExtractorTest, ThrowsExceptionWithInvalidHex) {
  const std::string invalid_version{"ZZ"};
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent",
       fmt::format("{}-{}-{}-{}", invalid_version, trace_id, parent_id, trace_flags)}};
  auto propagator = createDefaultPropagator();
  SpanContextExtractor span_context_extractor(request_headers, std::move(propagator));

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_FALSE(span_context.ok());
  EXPECT_THAT(span_context, HasStatusMessage("Invalid header hex"));
}

TEST_F(SpanContextExtractorTest, ThrowsExceptionWithAllZeroTraceId) {
  const std::string invalid_trace_id{"00000000000000000000000000000000"};
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent",
       fmt::format("{}-{}-{}-{}", version, invalid_trace_id, parent_id, trace_flags)}};
  auto propagator = createDefaultPropagator();
  SpanContextExtractor span_context_extractor(request_headers, std::move(propagator));

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_FALSE(span_context.ok());
  EXPECT_THAT(span_context, HasStatusMessage("Invalid trace id"));
}

TEST_F(SpanContextExtractorTest, ThrowsExceptionWithAllZeroParentId) {
  const std::string invalid_parent_id{"0000000000000000"};
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent",
       fmt::format("{}-{}-{}-{}", version, trace_id, invalid_parent_id, trace_flags)}};
  auto propagator = createDefaultPropagator();
  SpanContextExtractor span_context_extractor(request_headers, std::move(propagator));

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_FALSE(span_context.ok());
  EXPECT_THAT(span_context, HasStatusMessage("Invalid parent id"));
}

TEST_F(SpanContextExtractorTest, ExtractSpanContextWithEmptyTracestate) {
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", fmt::format("{}-{}-{}-{}", version, trace_id, parent_id, trace_flags)}};
  auto propagator = createDefaultPropagator();
  SpanContextExtractor span_context_extractor(request_headers, std::move(propagator));
  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_OK(span_context);
  EXPECT_TRUE(span_context->tracestate().empty());
}

TEST_F(SpanContextExtractorTest, ExtractSpanContextWithTracestate) {
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", fmt::format("{}-{}-{}-{}", version, trace_id, parent_id, trace_flags)},
      {"tracestate", "sample-tracestate"}};
  auto propagator = createDefaultPropagator();
  SpanContextExtractor span_context_extractor(request_headers, std::move(propagator));
  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_OK(span_context);
  EXPECT_EQ(span_context->tracestate(), "sample-tracestate");
}

TEST_F(SpanContextExtractorTest, IgnoreTracestateWithoutTraceparent) {
  Tracing::TestTraceContextImpl request_headers{{"tracestate", "sample-tracestate"}};
  auto propagator = createDefaultPropagator();
  SpanContextExtractor span_context_extractor(request_headers, std::move(propagator));
  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_FALSE(span_context.ok());
  EXPECT_THAT(span_context, HasStatusMessage("No propagator could extract span context"));
}

TEST_F(SpanContextExtractorTest, ExtractSpanContextWithMultipleTracestateEntries) {
  Http::TestRequestHeaderMapImpl request_headers{
      {"traceparent", fmt::format("{}-{}-{}-{}", version, trace_id, parent_id, trace_flags)},
      {"tracestate", "sample-tracestate"},
      {"tracestate", "sample-tracestate-2"}};
  Tracing::HttpTraceContext trace_context(request_headers);
  auto propagator = createDefaultPropagator();
  SpanContextExtractor span_context_extractor(trace_context, std::move(propagator));
  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_OK(span_context);
  EXPECT_EQ(span_context->tracestate(), "sample-tracestate,sample-tracestate-2");
}

} // namespace
} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
