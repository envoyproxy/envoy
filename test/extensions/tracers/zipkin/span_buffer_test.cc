#include "common/common/base64.h"
#include "common/protobuf/utility.h"

#include "extensions/tracers/zipkin/span_buffer.h"

#include "test/test_common/test_time.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {
namespace {

void expectValidBufferedProtoListOfSpans(const SpanBuffer& buffer) {
  std::string expected_yaml = "spans:";
  for (const auto& span : buffer.spans()) {
    const std::string expected_span_yaml = fmt::format(
        R"EOF(
- traceId: {}
  parentId: {}
  id: {}
)EOF",
        Base64::encode(span.traceIdAsByteString().c_str(), span.traceIdAsByteString().size()),
        span.isSetParentId() ? Base64::encode(span.parentIdAsByteString().c_str(),
                                              span.parentIdAsByteString().size())
                             : "",
        Base64::encode(span.idAsByteString().c_str(), span.idAsByteString().size()));
    expected_yaml += expected_span_yaml;
  }
  zipkin::proto3::ListOfSpans expected_msg;
  MessageUtil::loadFromYaml(expected_yaml, expected_msg);
  EXPECT_EQ(buffer.toProtoListOfSpans().DebugString(), expected_msg.DebugString());
}

TEST(ZipkinSpanBufferTest, defaultConstructorEndToEnd) {
  DangerousDeprecatedTestTime test_time;
  SpanBuffer buffer;

  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.toStringifiedJsonArray());
  EXPECT_EQ(buffer.toProtoListOfSpans().DebugString(), "");
  EXPECT_FALSE(buffer.addSpan(Span(test_time.timeSystem())));
  expectValidBufferedProtoListOfSpans(buffer);

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
  expectValidBufferedProtoListOfSpans(buffer);

  buffer.clear();
  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.toStringifiedJsonArray());
  expectValidBufferedProtoListOfSpans(buffer);

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
  expectValidBufferedProtoListOfSpans(buffer);

  buffer.clear();
  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.toStringifiedJsonArray());
  expectValidBufferedProtoListOfSpans(buffer);
}

TEST(ZipkinSpanBufferTest, sizeConstructorEndtoEnd) {
  DangerousDeprecatedTestTime test_time;
  SpanBuffer buffer(2);

  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.toStringifiedJsonArray());
  expectValidBufferedProtoListOfSpans(buffer);

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
  expectValidBufferedProtoListOfSpans(buffer);

  buffer.clear();
  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.toStringifiedJsonArray());
  expectValidBufferedProtoListOfSpans(buffer);

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
  expectValidBufferedProtoListOfSpans(buffer);

  buffer.clear();
  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.toStringifiedJsonArray());
  expectValidBufferedProtoListOfSpans(buffer);
}

} // namespace
} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
