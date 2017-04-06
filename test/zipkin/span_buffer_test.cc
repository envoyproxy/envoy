#include "zipkin/span_buffer.h"

#include "gtest/gtest.h"

namespace Zipkin {

TEST(ZipkinSpanBufferTest, defaultConstructorEndToEnd) {
  SpanBuffer buffer;
  Span span;

  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.toStringifiedJsonArray());

  buffer.allocateBuffer(2);
  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.toStringifiedJsonArray());

  buffer.addSpan(std::move(span));
  EXPECT_EQ(1ULL, buffer.pendingSpans());
  std::string expectedJsonArrayString = ""
                                        "[{\"traceId\":\"0000000000000000\","
                                        "\"name\":\"\","
                                        "\"id\":\"0000000000000000\","
                                        "\"annotations\":[],"
                                        "\"binaryAnnotations\":[]}"
                                        "]";
  EXPECT_EQ(expectedJsonArrayString, buffer.toStringifiedJsonArray());

  buffer.flush();
  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.toStringifiedJsonArray());

  buffer.addSpan(std::move(span));
  buffer.addSpan(Span(span));
  expectedJsonArrayString = ""
                            "["
                            "{\"traceId\":\"0000000000000000\","
                            "\"name\":\"\","
                            "\"id\":\"0000000000000000\","
                            "\"annotations\":[],"
                            "\"binaryAnnotations\":[]},"
                            "{\"traceId\":\"0000000000000000\","
                            "\"name\":\"\","
                            "\"id\":\"0000000000000000\","
                            "\"annotations\":[],"
                            "\"binaryAnnotations\":[]}"
                            "]";
  EXPECT_EQ(2ULL, buffer.pendingSpans());
  EXPECT_EQ(expectedJsonArrayString, buffer.toStringifiedJsonArray());

  buffer.flush();
  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.toStringifiedJsonArray());
}

TEST(ZipkinSpanBufferTest, sizeConstructorEndtoEnd) {
  SpanBuffer buffer(2);
  Span span;

  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.toStringifiedJsonArray());

  buffer.addSpan(std::move(span));
  EXPECT_EQ(1ULL, buffer.pendingSpans());
  std::string expectedJsonArrayString = ""
                                        "[{\"traceId\":\"0000000000000000\","
                                        "\"name\":\"\","
                                        "\"id\":\"0000000000000000\","
                                        "\"annotations\":[],"
                                        "\"binaryAnnotations\":[]}"
                                        "]";
  EXPECT_EQ(expectedJsonArrayString, buffer.toStringifiedJsonArray());

  buffer.flush();
  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.toStringifiedJsonArray());

  buffer.addSpan(std::move(span));
  buffer.addSpan(Span(span));
  expectedJsonArrayString = ""
                            "["
                            "{\"traceId\":\"0000000000000000\","
                            "\"name\":\"\","
                            "\"id\":\"0000000000000000\","
                            "\"annotations\":[],"
                            "\"binaryAnnotations\":[]},"
                            "{\"traceId\":\"0000000000000000\","
                            "\"name\":\"\","
                            "\"id\":\"0000000000000000\","
                            "\"annotations\":[],"
                            "\"binaryAnnotations\":[]}"
                            "]";
  EXPECT_EQ(2ULL, buffer.pendingSpans());
  EXPECT_EQ(expectedJsonArrayString, buffer.toStringifiedJsonArray());

  buffer.flush();
  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.toStringifiedJsonArray());
}
} // Zipkin
