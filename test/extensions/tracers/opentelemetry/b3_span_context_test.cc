#include "source/extensions/tracers/opentelemetry/b3_span_context.h"

#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {
namespace {

using StatusHelpers::HasStatusMessage;

constexpr char trace_id_128[] = "463ac35c9f6413ad48485a3953bb6124";
constexpr char trace_id_64[] = "48485a3953bb6124";
constexpr char padded_trace_id_64[] = "000000000000000048485a3953bb6124";
constexpr char span_id[] = "a2fb4a1d1a96d312";
constexpr char parent_span_id[] = "0020000000000001";

TEST(B3SpanContextTest, HeadersPresent) {
  EXPECT_FALSE(b3HeadersPresent(Tracing::TestTraceContextImpl{{"other", "value"}}));
  EXPECT_TRUE(b3HeadersPresent(
      Tracing::TestTraceContextImpl{{"b3", absl::StrCat(trace_id_128, "-", span_id)}}));
  EXPECT_TRUE(b3HeadersPresent(
      Tracing::TestTraceContextImpl{{"x-b3-traceid", trace_id_128}, {"x-b3-spanid", span_id}}));
  // Only one of the multiple header ids is not trace context.
  EXPECT_FALSE(b3HeadersPresent(Tracing::TestTraceContextImpl{{"x-b3-traceid", trace_id_128}}));
  EXPECT_FALSE(b3HeadersPresent(Tracing::TestTraceContextImpl{{"x-b3-spanid", span_id}}));
  // A single header that only carries a sampling decision has no trace context.
  EXPECT_FALSE(b3HeadersPresent(Tracing::TestTraceContextImpl{{"b3", "0"}}));
  EXPECT_FALSE(b3HeadersPresent(Tracing::TestTraceContextImpl{{"x-b3-sampled", "1"}}));
}

TEST(B3SpanContextTest, ExtractSingleHeader) {
  struct TestCase {
    std::string b3;
    std::string expected_trace_id;
    std::optional<bool> expected_sampled;
  };
  const std::vector<TestCase> test_cases = {
      {absl::StrCat(trace_id_128, "-", span_id), std::string(trace_id_128), std::nullopt},
      {absl::StrCat(trace_id_64, "-", span_id), std::string(padded_trace_id_64), std::nullopt},
      {absl::StrCat(trace_id_128, "-", span_id, "-1"), std::string(trace_id_128), true},
      {absl::StrCat(trace_id_128, "-", span_id, "-0"), std::string(trace_id_128), false},
      {absl::StrCat(trace_id_128, "-", span_id, "-d"), std::string(trace_id_128), true},
      {absl::StrCat(trace_id_128, "-", span_id, "-1-", parent_span_id), std::string(trace_id_128),
       true},
  };
  for (const auto& test_case : test_cases) {
    SCOPED_TRACE(test_case.b3);
    absl::StatusOr<B3SpanContext> span_context =
        extractB3SpanContext(Tracing::TestTraceContextImpl{{"b3", test_case.b3}});
    ASSERT_OK(span_context);
    EXPECT_EQ(span_context->trace_id, test_case.expected_trace_id);
    EXPECT_EQ(span_context->span_id, span_id);
    EXPECT_EQ(span_context->sampled, test_case.expected_sampled);
  }
}

TEST(B3SpanContextTest, ExtractInvalidSingleHeader) {
  struct TestCase {
    std::string b3;
    std::string expected_error;
  };
  const std::vector<TestCase> test_cases = {
      {"", "Invalid b3 header field count"},
      {std::string(trace_id_128), "Invalid b3 header field count"},
      {"1-2-3-4-5", "Invalid b3 header field count"},
      {absl::StrCat(trace_id_128, "-", span_id, "-1-", parent_span_id, "-"),
       "Invalid b3 header length"},
      {std::string(1024, '-'), "Invalid b3 header length"},
      {absl::StrCat("463ac35c9f6413a", "-", span_id), "Invalid b3 trace id"},
      {absl::StrCat("463ac35c9f6413ad48485a3953bb612", "-", span_id), "Invalid b3 trace id"},
      {absl::StrCat("x63ac35c9f6413ad48485a3953bb6124", "-", span_id), "Invalid b3 trace id"},
      {absl::StrCat("00000000000000000000000000000000", "-", span_id), "Invalid b3 trace id"},
      {absl::StrCat(trace_id_128, "-", "a2fb4a1d1a96d31"), "Invalid b3 span id"},
      {absl::StrCat(trace_id_128, "-", "x2fb4a1d1a96d312"), "Invalid b3 span id"},
      {absl::StrCat(trace_id_128, "-", "0000000000000000"), "Invalid b3 span id"},
      {absl::StrCat(trace_id_128, "-", span_id, "-2"), "Invalid b3 sampling state"},
      {absl::StrCat(trace_id_128, "-", span_id, "-"), "Invalid b3 sampling state"},
      {absl::StrCat(trace_id_128, "-", span_id, "-1-", "002000000000001"),
       "Invalid b3 parent span id"},
      {absl::StrCat(trace_id_128, "-", span_id, "-1-", "x020000000000001"),
       "Invalid b3 parent span id"},
  };
  for (const auto& test_case : test_cases) {
    SCOPED_TRACE(test_case.b3);
    EXPECT_THAT(extractB3SpanContext(Tracing::TestTraceContextImpl{{"b3", test_case.b3}}),
                HasStatusMessage(test_case.expected_error));
  }
}

TEST(B3SpanContextTest, ExtractMultipleHeaders) {
  struct TestCase {
    std::string trace_id;
    std::string expected_trace_id;
    std::vector<std::pair<std::string, std::string>> extra_headers;
    std::optional<bool> expected_sampled;
  };
  const std::vector<TestCase> test_cases = {
      {std::string(trace_id_128), std::string(trace_id_128), {}, std::nullopt},
      {std::string(trace_id_64), std::string(padded_trace_id_64), {}, std::nullopt},
      {std::string(trace_id_128), std::string(trace_id_128), {{"x-b3-sampled", "1"}}, true},
      {std::string(trace_id_128), std::string(trace_id_128), {{"x-b3-sampled", "true"}}, true},
      {std::string(trace_id_128), std::string(trace_id_128), {{"x-b3-sampled", "0"}}, false},
      {std::string(trace_id_128), std::string(trace_id_128), {{"x-b3-sampled", "false"}}, false},
      {std::string(trace_id_128),
       std::string(trace_id_128),
       {{"x-b3-flags", "1"}, {"x-b3-sampled", "0"}},
       true},
      {std::string(trace_id_128),
       std::string(trace_id_128),
       {{"x-b3-flags", "0"}, {"x-b3-sampled", "0"}},
       false},
      {std::string(trace_id_128),
       std::string(trace_id_128),
       {{"x-b3-parentspanid", std::string(parent_span_id)}},
       std::nullopt},
  };
  for (const auto& test_case : test_cases) {
    Tracing::TestTraceContextImpl trace_context{{"x-b3-traceid", test_case.trace_id},
                                                {"x-b3-spanid", std::string(span_id)}};
    for (const auto& [key, value] : test_case.extra_headers) {
      trace_context.set(key, value);
    }
    absl::StatusOr<B3SpanContext> span_context = extractB3SpanContext(trace_context);
    ASSERT_OK(span_context);
    EXPECT_EQ(span_context->trace_id, test_case.expected_trace_id);
    EXPECT_EQ(span_context->span_id, span_id);
    EXPECT_EQ(span_context->sampled, test_case.expected_sampled);
  }
}

TEST(B3SpanContextTest, ExtractInvalidMultipleHeaders) {
  EXPECT_THAT(extractB3SpanContext(Tracing::TestTraceContextImpl{{"x-b3-traceid", trace_id_128}}),
              HasStatusMessage("Missing x-b3-traceid or x-b3-spanid header"));
  EXPECT_THAT(extractB3SpanContext(Tracing::TestTraceContextImpl{{"x-b3-spanid", span_id}}),
              HasStatusMessage("Missing x-b3-traceid or x-b3-spanid header"));
  EXPECT_THAT(extractB3SpanContext(Tracing::TestTraceContextImpl{
                  {"x-b3-traceid", "0000000000000000"}, {"x-b3-spanid", span_id}}),
              HasStatusMessage("Invalid x-b3-traceid header"));
  EXPECT_THAT(extractB3SpanContext(Tracing::TestTraceContextImpl{{"x-b3-traceid", trace_id_128},
                                                                 {"x-b3-spanid", "invalid"}}),
              HasStatusMessage("Invalid x-b3-spanid header"));
}

TEST(B3SpanContextTest, SingleHeaderTakesPrecedence) {
  absl::StatusOr<B3SpanContext> span_context = extractB3SpanContext(
      Tracing::TestTraceContextImpl{{"b3", absl::StrCat(trace_id_128, "-", span_id, "-1")},
                                    {"x-b3-traceid", "0000000000000000000000000000000a"},
                                    {"x-b3-spanid", "000000000000000b"},
                                    {"x-b3-sampled", "0"}});
  ASSERT_OK(span_context);
  EXPECT_EQ(span_context->trace_id, trace_id_128);
  EXPECT_EQ(span_context->span_id, span_id);
  EXPECT_EQ(span_context->sampled, true);
}

TEST(B3SpanContextTest, MalformedSingleHeaderFallsBackToMultipleHeaders) {
  absl::StatusOr<B3SpanContext> span_context = extractB3SpanContext(
      Tracing::TestTraceContextImpl{{"b3", "invalid"},
                                    {"x-b3-traceid", std::string(trace_id_128)},
                                    {"x-b3-spanid", std::string(span_id)},
                                    {"x-b3-sampled", "1"}});
  ASSERT_OK(span_context);
  EXPECT_EQ(span_context->trace_id, trace_id_128);
  EXPECT_EQ(span_context->sampled, true);
}

TEST(B3SpanContextTest, SamplingOnlySingleHeaderUsesMultipleHeaders) {
  absl::StatusOr<B3SpanContext> span_context = extractB3SpanContext(
      Tracing::TestTraceContextImpl{{"b3", "1"},
                                    {"x-b3-traceid", std::string(trace_id_128)},
                                    {"x-b3-spanid", std::string(span_id)}});
  ASSERT_OK(span_context);
  EXPECT_EQ(span_context->trace_id, trace_id_128);
  EXPECT_EQ(span_context->sampled, std::nullopt);
}

TEST(B3SpanContextTest, InjectSingleHeader) {
  Tracing::TestTraceContextImpl sampled_context{};
  injectB3SingleHeader(sampled_context, trace_id_128, span_id, true);
  EXPECT_EQ(sampled_context.get("b3"), absl::StrCat(trace_id_128, "-", span_id, "-1"));

  Tracing::TestTraceContextImpl not_sampled_context{};
  injectB3SingleHeader(not_sampled_context, trace_id_128, span_id, false);
  EXPECT_EQ(not_sampled_context.get("b3"), absl::StrCat(trace_id_128, "-", span_id, "-0"));
}

TEST(B3SpanContextTest, InjectMultipleHeaders) {
  Tracing::TestTraceContextImpl trace_context{{"x-b3-parentspanid", std::string(parent_span_id)},
                                              {"x-b3-flags", "1"}};
  injectB3MultiHeaders(trace_context, trace_id_128, span_id, true);
  EXPECT_EQ(trace_context.get("x-b3-traceid"), trace_id_128);
  EXPECT_EQ(trace_context.get("x-b3-spanid"), span_id);
  EXPECT_EQ(trace_context.get("x-b3-sampled"), "1");
  EXPECT_FALSE(trace_context.get("x-b3-parentspanid").has_value());
  EXPECT_FALSE(trace_context.get("x-b3-flags").has_value());

  Tracing::TestTraceContextImpl not_sampled_context{};
  injectB3MultiHeaders(not_sampled_context, trace_id_128, span_id, false);
  EXPECT_EQ(not_sampled_context.get("x-b3-sampled"), "0");
}

} // namespace
} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
