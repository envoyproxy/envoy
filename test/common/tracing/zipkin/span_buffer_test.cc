#include "common/tracing/zipkin/span_buffer.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Zipkin {

TEST(ZipkinSpanBufferTest, defaultConstructorEndToEnd) {
  SpanBuffer buffer;

  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.toStringifiedJsonArray());

  buffer.allocateBuffer(2);
  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.toStringifiedJsonArray());

  buffer.addSpan(Span());
  EXPECT_EQ(1ULL, buffer.pendingSpans());
  std::string expected_json_array_string = "[{"
                                           R"("traceId":"0000000000000000",)"
                                           R"("name":"",)"
                                           R"("id":"0000000000000000",)"
                                           R"("annotations":[],)"
                                           R"("binaryAnnotations":[])"
                                           "}]";
  EXPECT_EQ(expected_json_array_string, buffer.toStringifiedJsonArray());

  buffer.clear();
  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.toStringifiedJsonArray());

  buffer.addSpan(Span());
  buffer.addSpan(Span());
  expected_json_array_string = "["
                               "{"
                               R"("traceId":"0000000000000000",)"
                               R"("name":"",)"
                               R"("id":"0000000000000000",)"
                               R"("annotations":[],)"
                               R"("binaryAnnotations":[])"
                               "},"
                               "{"
                               R"("traceId":"0000000000000000",)"
                               R"("name":"",)"
                               R"("id":"0000000000000000",)"
                               R"("annotations":[],)"
                               R"("binaryAnnotations":[])"
                               "}"
                               "]";
  EXPECT_EQ(2ULL, buffer.pendingSpans());
  EXPECT_EQ(expected_json_array_string, buffer.toStringifiedJsonArray());

  buffer.clear();
  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.toStringifiedJsonArray());
}

TEST(ZipkinSpanBufferTest, sizeConstructorEndtoEnd) {
  SpanBuffer buffer(2);

  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.toStringifiedJsonArray());

  buffer.addSpan(Span());
  EXPECT_EQ(1ULL, buffer.pendingSpans());
  std::string expected_json_array_string = "[{"
                                           R"("traceId":"0000000000000000",)"
                                           R"("name":"",)"
                                           R"("id":"0000000000000000",)"
                                           R"("annotations":[],)"
                                           R"("binaryAnnotations":[])"
                                           "}]";
  EXPECT_EQ(expected_json_array_string, buffer.toStringifiedJsonArray());

  buffer.clear();
  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.toStringifiedJsonArray());

  buffer.addSpan(Span());
  buffer.addSpan(Span());
  expected_json_array_string = "["
                               "{"
                               R"("traceId":"0000000000000000",)"
                               R"("name":"",)"
                               R"("id":"0000000000000000",)"
                               R"("annotations":[],)"
                               R"("binaryAnnotations":[])"
                               "},"
                               "{"
                               R"("traceId":"0000000000000000",)"
                               R"("name":"",)"
                               R"("id":"0000000000000000",)"
                               R"("annotations":[],)"
                               R"("binaryAnnotations":[])"
                               "}]";
  EXPECT_EQ(2ULL, buffer.pendingSpans());
  EXPECT_EQ(expected_json_array_string, buffer.toStringifiedJsonArray());

  buffer.clear();
  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.toStringifiedJsonArray());
}
} // namespace Zipkin
} // namespace Envoy
