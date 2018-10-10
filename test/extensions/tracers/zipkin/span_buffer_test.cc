#include "extensions/tracers/zipkin/span_buffer.h"

#include "test/test_common/test_time.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

TEST(ZipkinSpanBufferTest, defaultConstructorEndToEnd) {
  DangerousDeprecatedTestTime test_time;
  SpanBuffer buffer;

  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.toStringifiedJsonArray());
  EXPECT_FALSE(buffer.addSpan(Span(test_time.timeSystem())));

  buffer.allocateBuffer(2);
  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.toStringifiedJsonArray());

  buffer.addSpan(Span(test_time.timeSystem()));
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

  buffer.addSpan(Span(test_time.timeSystem()));
  buffer.addSpan(Span(test_time.timeSystem()));
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
  DangerousDeprecatedTestTime test_time;
  SpanBuffer buffer(2);

  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.toStringifiedJsonArray());

  buffer.addSpan(Span(test_time.timeSystem()));
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

  buffer.addSpan(Span(test_time.timeSystem()));
  buffer.addSpan(Span(test_time.timeSystem()));
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
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
