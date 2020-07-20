#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/zipkin/span_context.h"
#include "extensions/tracers/zipkin/span_context_extractor.h"

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
  Http::TestRequestHeaderMapImpl request_headers{
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
  EXPECT_TRUE(extractor.extractSampled({Tracing::Reason::Sampling, false}));
}

TEST(ZipkinSpanContextExtractorTest, WithoutParentDebug) {
  Http::TestRequestHeaderMapImpl request_headers{
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
  EXPECT_TRUE(extractor.extractSampled({Tracing::Reason::Sampling, false}));
}

TEST(ZipkinSpanContextExtractorTest, MalformedUuid) {
  Http::TestRequestHeaderMapImpl request_headers{{"b3", "b970dafd-0d95-40aa-95d8-1d8725aebe40"}};
  SpanContextExtractor extractor(request_headers);
  EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                            "Invalid input: invalid trace id b970dafd-0d95-40");
  EXPECT_TRUE(extractor.extractSampled({Tracing::Reason::Sampling, true}));
}

TEST(ZipkinSpanContextExtractorTest, MiddleOfString) {
  Http::TestRequestHeaderMapImpl request_headers{
      {"b3", fmt::format("{}{}-{},", trace_id, trace_id, span_id)}};
  SpanContextExtractor extractor(request_headers);
  EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                            "Invalid input: truncated");
  EXPECT_TRUE(extractor.extractSampled({Tracing::Reason::Sampling, true}));
}

TEST(ZipkinSpanContextExtractorTest, DebugOnly) {
  Http::TestRequestHeaderMapImpl request_headers{{"b3", "d"}};
  SpanContextExtractor extractor(request_headers);
  auto context = extractor.extractSpanContext(true);
  EXPECT_FALSE(context.second);
  EXPECT_EQ(0, context.first.id());
  EXPECT_EQ(0, context.first.parentId());
  EXPECT_FALSE(context.first.is128BitTraceId());
  EXPECT_EQ(0, context.first.traceId());
  EXPECT_EQ(0, context.first.traceIdHigh());
  EXPECT_FALSE(context.first.sampled());
  EXPECT_TRUE(extractor.extractSampled({Tracing::Reason::Sampling, false}));
}

TEST(ZipkinSpanContextExtractorTest, Sampled) {
  Http::TestRequestHeaderMapImpl request_headers{{"b3", "1"}};
  SpanContextExtractor extractor(request_headers);
  auto context = extractor.extractSpanContext(true);
  EXPECT_FALSE(context.second);
  EXPECT_EQ(0, context.first.id());
  EXPECT_EQ(0, context.first.parentId());
  EXPECT_FALSE(context.first.is128BitTraceId());
  EXPECT_EQ(0, context.first.traceId());
  EXPECT_EQ(0, context.first.traceIdHigh());
  EXPECT_FALSE(context.first.sampled());
  EXPECT_TRUE(extractor.extractSampled({Tracing::Reason::Sampling, false}));
}

TEST(ZipkinSpanContextExtractorTest, SampledFalse) {
  Http::TestRequestHeaderMapImpl request_headers{{"b3", "0"}};
  SpanContextExtractor extractor(request_headers);
  auto context = extractor.extractSpanContext(true);
  EXPECT_FALSE(context.second);
  EXPECT_EQ(0, context.first.id());
  EXPECT_EQ(0, context.first.parentId());
  EXPECT_FALSE(context.first.is128BitTraceId());
  EXPECT_EQ(0, context.first.traceId());
  EXPECT_EQ(0, context.first.traceIdHigh());
  EXPECT_FALSE(context.first.sampled());
  EXPECT_FALSE(extractor.extractSampled({Tracing::Reason::Sampling, true}));
}

TEST(ZipkinSpanContextExtractorTest, IdNotYetSampled128) {
  Http::TestRequestHeaderMapImpl request_headers{
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
  EXPECT_FALSE(extractor.extractSampled({Tracing::Reason::Sampling, false}));
}

TEST(ZipkinSpanContextExtractorTest, IdsUnsampled) {
  Http::TestRequestHeaderMapImpl request_headers{{"b3", fmt::format("{}-{}-0", trace_id, span_id)}};
  SpanContextExtractor extractor(request_headers);
  auto context = extractor.extractSpanContext(true);
  EXPECT_TRUE(context.second);
  EXPECT_EQ(3, context.first.id());
  EXPECT_EQ(0, context.first.parentId());
  EXPECT_FALSE(context.first.is128BitTraceId());
  EXPECT_EQ(1, context.first.traceId());
  EXPECT_EQ(0, context.first.traceIdHigh());
  EXPECT_TRUE(context.first.sampled());
  EXPECT_FALSE(extractor.extractSampled({Tracing::Reason::Sampling, true}));
}

TEST(ZipkinSpanContextExtractorTest, ParentUnsampled) {
  Http::TestRequestHeaderMapImpl request_headers{
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
  EXPECT_FALSE(extractor.extractSampled({Tracing::Reason::Sampling, true}));
}

TEST(ZipkinSpanContextExtractorTest, ParentDebug) {
  Http::TestRequestHeaderMapImpl request_headers{
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
  EXPECT_TRUE(extractor.extractSampled({Tracing::Reason::Sampling, false}));
}

TEST(ZipkinSpanContextExtractorTest, IdsWithDebug) {
  Http::TestRequestHeaderMapImpl request_headers{{"b3", fmt::format("{}-{}-d", trace_id, span_id)}};
  SpanContextExtractor extractor(request_headers);
  auto context = extractor.extractSpanContext(true);
  EXPECT_TRUE(context.second);
  EXPECT_EQ(3, context.first.id());
  EXPECT_EQ(0, context.first.parentId());
  EXPECT_FALSE(context.first.is128BitTraceId());
  EXPECT_EQ(1, context.first.traceId());
  EXPECT_EQ(0, context.first.traceIdHigh());
  EXPECT_TRUE(context.first.sampled());
  EXPECT_TRUE(extractor.extractSampled({Tracing::Reason::Sampling, false}));
}

TEST(ZipkinSpanContextExtractorTest, WithoutSampled) {
  Http::TestRequestHeaderMapImpl request_headers{{"b3", fmt::format("{}-{}", trace_id, span_id)}};
  SpanContextExtractor extractor(request_headers);
  auto context = extractor.extractSpanContext(false);
  EXPECT_TRUE(context.second);
  EXPECT_EQ(3, context.first.id());
  EXPECT_EQ(0, context.first.parentId());
  EXPECT_FALSE(context.first.is128BitTraceId());
  EXPECT_EQ(1, context.first.traceId());
  EXPECT_EQ(0, context.first.traceIdHigh());
  EXPECT_FALSE(context.first.sampled());
  EXPECT_TRUE(extractor.extractSampled({Tracing::Reason::Sampling, true}));
}

TEST(ZipkinSpanContextExtractorTest, TooBig) {
  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"b3", fmt::format("{}{}{}-{}-{}", trace_id, trace_id, trace_id, span_id, trace_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: too long");
    EXPECT_FALSE(extractor.extractSampled({Tracing::Reason::Sampling, false}));
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"b3", fmt::format("{}{}-{}-1-{}a", trace_id_high, trace_id, span_id, parent_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: too long");
  }
}

TEST(ZipkinSpanContextExtractorTest, Empty) {
  Http::TestRequestHeaderMapImpl request_headers{{"b3", ""}};
  SpanContextExtractor extractor(request_headers);
  EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                            "Invalid input: empty");
}

TEST(ZipkinSpanContextExtractorTest, InvalidInput) {
  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"X-B3-TraceId", trace_id_high + trace_id.substr(0, 15) + "!"}, {"X-B3-SpanId", span_id}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              fmt::format("Invalid traceid_high {} or tracid {}", trace_id_high,
                                          trace_id.substr(0, 15) + "!"));
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"b3", fmt::format("{}!{}-{}", trace_id.substr(0, 15), trace_id, span_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(
        extractor.extractSpanContext(true), ExtractorException,
        fmt::format("Invalid input: invalid trace id high {}!", trace_id.substr(0, 15)));
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"b3", fmt::format("{}{}!-{}", trace_id, trace_id.substr(0, 15), span_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(
        extractor.extractSpanContext(true), ExtractorException,
        fmt::format("Invalid input: invalid trace id {}!", trace_id.substr(0, 15)));
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"b3", fmt::format("{}!-{}", trace_id.substr(0, 15), span_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(
        extractor.extractSpanContext(true), ExtractorException,
        fmt::format("Invalid input: invalid trace id {}!", trace_id.substr(0, 15)));
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{{"b3", fmt::format("{}!{}", trace_id, span_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: not exists span id");
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"b3", fmt::format("{}-{}!", trace_id, span_id.substr(0, 15))}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(
        extractor.extractSpanContext(true), ExtractorException,
        fmt::format("Invalid input: invalid span id {}!", span_id.substr(0, 15)));
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"b3", fmt::format("{}-{}!0", trace_id, span_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: not exists sampling field");
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"b3", fmt::format("{}-{}-c", trace_id, span_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: invalid sampling flag c");
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"b3", fmt::format("{}-{}-d!{}", trace_id, span_id, parent_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"b3", fmt::format("{}-{}-d-{}!", trace_id, span_id, parent_id.substr(0, 15))}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(
        extractor.extractSpanContext(true), ExtractorException,
        fmt::format("Invalid input: invalid parent id {}!", parent_id.substr(0, 15)));
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{{"b3", "-"}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_TRUE(extractor.extractSampled({Tracing::Reason::Sampling, true}));
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: invalid sampling flag -");
  }
}

TEST(ZipkinSpanContextExtractorTest, Truncated) {
  {
    Http::TestRequestHeaderMapImpl request_headers{{"b3", "-1"}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{{"b3", "1-"}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{{"b3", "1-"}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{{"b3", trace_id.substr(0, 15)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{{"b3", trace_id}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{{"b3", trace_id + "-"}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"b3", fmt::format("{}-{}", trace_id.substr(0, 15), span_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"b3", fmt::format("{}-{}", trace_id, span_id.substr(0, 15))}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"b3", fmt::format("{}-{}-", trace_id, span_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"b3", fmt::format("{}-{}-1-", trace_id, span_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"b3", fmt::format("{}-{}-1-{}", trace_id, span_id, parent_id.substr(0, 15))}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }

  {
    Http::TestRequestHeaderMapImpl request_headers{
        {"b3", fmt::format("{}-{}-{}{}", trace_id, span_id, trace_id, trace_id)}};
    SpanContextExtractor extractor(request_headers);
    EXPECT_THROW_WITH_MESSAGE(extractor.extractSpanContext(true), ExtractorException,
                              "Invalid input: truncated");
  }
}

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
