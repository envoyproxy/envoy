#include "source/common/http/sse/sse_parser.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {
namespace Sse {
namespace {

class SseParserTest : public testing::Test {};

// Test parseEvent with single data field
TEST_F(SseParserTest, ParseEventSingle) {
  const std::string event = "data: hello world\n";
  auto parsed = SseParser::parseEvent(event);
  ASSERT_TRUE(parsed.data.has_value());
  EXPECT_EQ(parsed.data.value(), "hello world");
}

// Test parseEvent with multiple data fields
TEST_F(SseParserTest, ParseEventMultiple) {
  const std::string event = "data: first line\ndata: second line\n";
  auto parsed = SseParser::parseEvent(event);
  ASSERT_TRUE(parsed.data.has_value());
  EXPECT_EQ(parsed.data.value(), "first line\nsecond line");
}

// Test parseEvent with no data field
TEST_F(SseParserTest, ParseEventNone) {
  const std::string event = "event: ping\nid: 123\n";
  auto parsed = SseParser::parseEvent(event);
  EXPECT_FALSE(parsed.data.has_value());
}

// Test parseEvent with empty data field
TEST_F(SseParserTest, ParseEventEmpty) {
  const std::string event = "data:\n";
  auto parsed = SseParser::parseEvent(event);
  ASSERT_TRUE(parsed.data.has_value());
  EXPECT_EQ(parsed.data.value(), "");
}

// Test parseEvent with data field without space after colon
TEST_F(SseParserTest, ParseEventNoSpace) {
  const std::string event = "data:nospace\n";
  auto parsed = SseParser::parseEvent(event);
  ASSERT_TRUE(parsed.data.has_value());
  EXPECT_EQ(parsed.data.value(), "nospace");
}

// Test parseEvent with comment lines
TEST_F(SseParserTest, ParseEventWithComments) {
  const std::string event = ": comment line\ndata: actual data\n: another comment\n";
  auto parsed = SseParser::parseEvent(event);
  ASSERT_TRUE(parsed.data.has_value());
  EXPECT_EQ(parsed.data.value(), "actual data");
}

// Test parseEvent with mixed fields
TEST_F(SseParserTest, ParseEventMixed) {
  const std::string event = "event: message\ndata: content\nid: 42\ndata: more content\n";
  auto parsed = SseParser::parseEvent(event);
  ASSERT_TRUE(parsed.data.has_value());
  EXPECT_EQ(parsed.data.value(), "content\nmore content");
}

// Test parseEvent with CRLF line endings
TEST_F(SseParserTest, ParseEventCRLF) {
  const std::string event = "data: test\r\n";
  auto parsed = SseParser::parseEvent(event);
  ASSERT_TRUE(parsed.data.has_value());
  EXPECT_EQ(parsed.data.value(), "test");
}

// Test parseEvent with CR line endings
TEST_F(SseParserTest, ParseEventCR) {
  const std::string event = "data: test\r";
  auto parsed = SseParser::parseEvent(event);
  ASSERT_TRUE(parsed.data.has_value());
  EXPECT_EQ(parsed.data.value(), "test");
}

// Test findEventEnd with complete event (double newline)
TEST_F(SseParserTest, FindEventEndComplete) {
  const std::string buffer = "data: test\n\nmore data";
  auto [event_start, event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_start, 0);
  EXPECT_EQ(event_end, 11);  // Position before the second newline
  EXPECT_EQ(next_event, 12); // Position after the second newline
}

// Test findEventEnd with incomplete event
TEST_F(SseParserTest, FindEventEndIncomplete) {
  const std::string buffer = "data: test\n";
  auto [event_start, event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_start, absl::string_view::npos);
  EXPECT_EQ(event_end, absl::string_view::npos);
  EXPECT_EQ(next_event, absl::string_view::npos);
}

// Test findEventEnd with end_stream and incomplete event
TEST_F(SseParserTest, FindEventEndEndStream) {
  const std::string buffer = "data: test\n";
  auto [event_start, event_end, next_event] = SseParser::findEventEnd(buffer, true);
  // With end_stream, incomplete event should not be found (still needs blank line)
  EXPECT_EQ(event_start, absl::string_view::npos);
  EXPECT_EQ(event_end, absl::string_view::npos);
  EXPECT_EQ(next_event, absl::string_view::npos);
}

// Test findEventEnd with end_stream and blank line
TEST_F(SseParserTest, FindEventEndEndStreamWithBlankLine) {
  const std::string buffer = "data: test\n\n";
  auto [event_start, event_end, next_event] = SseParser::findEventEnd(buffer, true);
  EXPECT_EQ(event_start, 0);
  EXPECT_EQ(event_end, 11);
  EXPECT_EQ(next_event, 12);
}

// Test findEventEnd with CRLF blank line
TEST_F(SseParserTest, FindEventEndCRLF) {
  const std::string buffer = "data: test\r\n\r\n";
  auto [event_start, event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_start, 0);
  EXPECT_EQ(event_end, 12);
  EXPECT_EQ(next_event, 14);
}

// Test findEventEnd with mixed line endings
TEST_F(SseParserTest, FindEventEndMixed) {
  const std::string buffer = "data: test\r\n\n";
  auto [event_start, event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_start, 0);
  EXPECT_EQ(event_end, 12);
  EXPECT_EQ(next_event, 13);
}

// Test findEventEnd with CR line endings
TEST_F(SseParserTest, FindEventEndCR) {
  const std::string buffer = "data: test\r\rmore";
  auto [event_start, event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_start, 0);
  EXPECT_EQ(event_end, 11);
  EXPECT_EQ(next_event, 12);
}

// Test findEventEnd with multiple events
TEST_F(SseParserTest, FindEventEndMultiple) {
  const std::string buffer = "data: first\n\ndata: second\n\n";
  auto [event_start, event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_start, 0);
  EXPECT_EQ(event_end, 12);
  EXPECT_EQ(next_event, 13);

  // Find second event
  auto [event_start2, event_end2, next_event2] =
      SseParser::findEventEnd(buffer.substr(next_event), false);
  EXPECT_EQ(event_start2, 0);
  EXPECT_EQ(event_end2, 13);
  EXPECT_EQ(next_event2, 14);
}

// Test findEventEnd with empty buffer
TEST_F(SseParserTest, FindEventEndEmpty) {
  const std::string buffer = "";
  auto [event_start, event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_start, absl::string_view::npos);
  EXPECT_EQ(event_end, absl::string_view::npos);
  EXPECT_EQ(next_event, absl::string_view::npos);
}

// Test findEventEnd with only blank lines
TEST_F(SseParserTest, FindEventEndOnlyBlankLines) {
  const std::string buffer = "\n\n";
  auto [event_start, event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_start, 0);
  EXPECT_EQ(event_end, 0);
  EXPECT_EQ(next_event, 1);
}

// Test findEventEnd with comment before blank line
TEST_F(SseParserTest, FindEventEndWithComment) {
  const std::string buffer = ": comment\ndata: test\n\n";
  auto [event_start, event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_start, 0);
  EXPECT_EQ(event_end, 21);
  EXPECT_EQ(next_event, 22);
}

// Test findEventEnd with trailing CR needing CRLF check
TEST_F(SseParserTest, FindEventEndTrailingCR) {
  const std::string buffer = "data: test\r";
  auto [event_start, event_end, next_event] = SseParser::findEventEnd(buffer, false);
  // Should wait for potential LF
  EXPECT_EQ(event_start, absl::string_view::npos);
  EXPECT_EQ(event_end, absl::string_view::npos);
  EXPECT_EQ(next_event, absl::string_view::npos);
}

// Test findEventEnd with trailing CR and end_stream
TEST_F(SseParserTest, FindEventEndTrailingCREndStream) {
  const std::string buffer = "data: test\r";
  auto [event_start, event_end, next_event] = SseParser::findEventEnd(buffer, true);
  // With end_stream, should treat CR as line ending but still need blank line
  EXPECT_EQ(event_start, absl::string_view::npos);
  EXPECT_EQ(event_end, absl::string_view::npos);
  EXPECT_EQ(next_event, absl::string_view::npos);
}

// Test parseEvent with JSON content
TEST_F(SseParserTest, ParseEventJSON) {
  const std::string event = "data: {\"key\":\"value\"}\n";
  auto parsed = SseParser::parseEvent(event);
  ASSERT_TRUE(parsed.data.has_value());
  EXPECT_EQ(parsed.data.value(), "{\"key\":\"value\"}");
}

// Test parseEvent with multiline JSON (multiple data fields)
TEST_F(SseParserTest, ParseEventMultilineJSON) {
  const std::string event = "data: {\"start\":\n"
                            "data: \"middle\",\n"
                            "data: \"end\":true}\n";
  auto parsed = SseParser::parseEvent(event);
  ASSERT_TRUE(parsed.data.has_value());
  EXPECT_EQ(parsed.data.value(), "{\"start\":\n\"middle\",\n\"end\":true}");
}

// Test field line with colon in value
TEST_F(SseParserTest, ParseEventColonInValue) {
  const std::string event = "data: http://example.com\n";
  auto parsed = SseParser::parseEvent(event);
  ASSERT_TRUE(parsed.data.has_value());
  EXPECT_EQ(parsed.data.value(), "http://example.com");
}

// Test field line without colon
TEST_F(SseParserTest, ParseEventNoColon) {
  const std::string event = "dataonly\n";
  auto parsed = SseParser::parseEvent(event);
  EXPECT_FALSE(parsed.data.has_value());
}

// Test with very long data field
TEST_F(SseParserTest, ParseEventLong) {
  std::string long_data(10000, 'x');
  const std::string event = "data: " + long_data + "\n";
  auto parsed = SseParser::parseEvent(event);
  ASSERT_TRUE(parsed.data.has_value());
  EXPECT_EQ(parsed.data.value(), long_data);
}

// Test parseEvent with Unicode
TEST_F(SseParserTest, ParseEventUnicode) {
  const std::string event = "data: Hello 世界 🌍\n";
  auto parsed = SseParser::parseEvent(event);
  ASSERT_TRUE(parsed.data.has_value());
  EXPECT_EQ(parsed.data.value(), "Hello 世界 🌍");
}

// Test parseEvent with null bytes
TEST_F(SseParserTest, ParseEventNullBytes) {
  const std::string event = std::string("data: hello\0world\n", 18);
  auto parsed = SseParser::parseEvent(event);
  ASSERT_TRUE(parsed.data.has_value());
  EXPECT_EQ(parsed.data.value().size(), 11);
  EXPECT_EQ(parsed.data.value(), std::string("hello\0world", 11));
}

// Test parseEvent with data field followed by whitespace
TEST_F(SseParserTest, ParseEventTrailingSpace) {
  const std::string event = "data: value \n";
  auto parsed = SseParser::parseEvent(event);
  ASSERT_TRUE(parsed.data.has_value());
  EXPECT_EQ(parsed.data.value(), "value ");
}

// Test parseEvent with multiple spaces after colon
TEST_F(SseParserTest, ParseEventMultipleSpaces) {
  const std::string event = "data:  extra spaces\n";
  auto parsed = SseParser::parseEvent(event);
  ASSERT_TRUE(parsed.data.has_value());
  EXPECT_EQ(parsed.data.value(), " extra spaces");
}

// Test parseEvent with tab after colon
// Per SSE spec, only space character (not tab) is stripped
TEST_F(SseParserTest, ParseEventTab) {
  const std::string event = "data:\tvalue\n";
  auto parsed = SseParser::parseEvent(event);
  ASSERT_TRUE(parsed.data.has_value());
  EXPECT_EQ(parsed.data.value(), "\tvalue");
}

// Test findEventEnd with three consecutive blank lines
TEST_F(SseParserTest, FindEventEndTripleBlankLines) {
  const std::string buffer = "data: test\n\n\n";
  auto [event_start, event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_start, 0);
  EXPECT_EQ(event_end, 11);
  EXPECT_EQ(next_event, 12);
}

// Test findEventEnd with buffer starting with blank line
TEST_F(SseParserTest, FindEventEndStartsWithBlankLine) {
  const std::string buffer = "\ndata: test\n\n";
  auto [event_start, event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_start, 0);
  EXPECT_EQ(event_end, 0);
  EXPECT_EQ(next_event, 1);
}

// Test findEventEnd with CRLF then LF
TEST_F(SseParserTest, FindEventEndCRLFThenLF) {
  const std::string buffer = "data: test\r\n\n";
  auto [event_start, event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_start, 0);
  EXPECT_EQ(event_end, 12);
  EXPECT_EQ(next_event, 13);
}

// Test findEventEnd with LF then CRLF
TEST_F(SseParserTest, FindEventEndLFThenCRLF) {
  const std::string buffer = "data: test\n\r\n";
  auto [event_start, event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_start, 0);
  EXPECT_EQ(event_end, 11);
  EXPECT_EQ(next_event, 13);
}

// Test findEventEnd with CR, CR (double CR)
TEST_F(SseParserTest, FindEventEndDoubleCR) {
  const std::string buffer = "data: test\r\rmore";
  auto [event_start, event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_start, 0);
  EXPECT_EQ(event_end, 11);
  EXPECT_EQ(next_event, 12);
}

// Test parseEvent with field name containing whitespace
TEST_F(SseParserTest, ParseEventWhitespaceInFieldName) {
  const std::string event = "data extra: value\n";
  // Per SSE spec, field name is until first colon, so "data extra" is the field name
  auto parsed = SseParser::parseEvent(event);
  EXPECT_FALSE(parsed.data.has_value());
}

// Test parseEvent with only colon
TEST_F(SseParserTest, ParseEventOnlyColon) {
  const std::string event = ":\n";
  auto parsed = SseParser::parseEvent(event);
  EXPECT_FALSE(parsed.data.has_value());
}

// Test parseEvent with data field mixed with event field
TEST_F(SseParserTest, ParseEventWithEventField) {
  const std::string event = "event: custom\ndata: value\n";
  auto parsed = SseParser::parseEvent(event);
  ASSERT_TRUE(parsed.data.has_value());
  EXPECT_EQ(parsed.data.value(), "value");
}

// Test findEventEnd with very long line
TEST_F(SseParserTest, FindEventEndLongLine) {
  std::string buffer = "data: " + std::string(10000, 'x') + "\n\n";
  auto [event_start, event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_start, 0);
  EXPECT_EQ(event_end, 10007);
  EXPECT_EQ(next_event, 10008);
}

// Test parseEvent with multiple data fields separated by other fields
TEST_F(SseParserTest, ParseEventInterspersed) {
  const std::string event = "data: first\nid: 123\ndata: second\nevent: msg\ndata: third\n";
  auto parsed = SseParser::parseEvent(event);
  ASSERT_TRUE(parsed.data.has_value());
  EXPECT_EQ(parsed.data.value(), "first\nsecond\nthird");
}

// Test findEventEnd with only comments
TEST_F(SseParserTest, FindEventEndOnlyComments) {
  const std::string buffer = ": comment 1\n: comment 2\n\n";
  auto [event_start, event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_start, 0);
  EXPECT_EQ(event_end, 24);
  EXPECT_EQ(next_event, 25);
}

// Test parseEvent with empty string
TEST_F(SseParserTest, ParseEventEmptyString) {
  const std::string event = "";
  auto parsed = SseParser::parseEvent(event);
  EXPECT_FALSE(parsed.data.has_value());
}

// Test findEventEnd with single newline at end
TEST_F(SseParserTest, FindEventEndSingleNewlineAtEnd) {
  const std::string buffer = "data: test\n";
  auto [event_start, event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_start, absl::string_view::npos);
  EXPECT_EQ(event_end, absl::string_view::npos);
  EXPECT_EQ(next_event, absl::string_view::npos);
}

// Test findEventEnd with trailing CR followed by more data
TEST_F(SseParserTest, FindEventEndTrailingCRWithData) {
  const std::string buffer = "data: test\rmore data";
  auto [event_start, event_end, next_event] = SseParser::findEventEnd(buffer, false);
  // Should find CR as line ending
  EXPECT_EQ(event_start, absl::string_view::npos);
  EXPECT_EQ(event_end, absl::string_view::npos);
  EXPECT_EQ(next_event, absl::string_view::npos);
}

// Test parseEvent with empty lines to exercise parseFieldLine empty line case
TEST_F(SseParserTest, ParseEventWithEmptyLines) {
  const std::string event = "\ndata: test\n\n";
  auto parsed = SseParser::parseEvent(event);
  ASSERT_TRUE(parsed.data.has_value());
  EXPECT_EQ(parsed.data.value(), "test");
}

// Test parseEvent with no line ending (exercises findLineEnd with end_stream=true)
TEST_F(SseParserTest, ParseEventNoLineEnding) {
  const std::string event = "data: test";
  auto parsed = SseParser::parseEvent(event);
  ASSERT_TRUE(parsed.data.has_value());
  EXPECT_EQ(parsed.data.value(), "test");
}

// Test findEventEnd with data without newline and end_stream=true
TEST_F(SseParserTest, FindEventEndNoLineEndingEndStream) {
  const std::string buffer = "data: test";
  auto [event_start, event_end, next_event] = SseParser::findEventEnd(buffer, true);
  EXPECT_EQ(event_start, absl::string_view::npos);
  EXPECT_EQ(event_end, absl::string_view::npos);
  EXPECT_EQ(next_event, absl::string_view::npos);
}

TEST_F(SseParserTest, FindEventEndWithBOM) {
  // UTF-8 BOM (0xEF 0xBB 0xBF) should be stripped at stream start
  const std::string buffer = std::string("\xEF\xBB\xBF") + "data: hello\n\n";
  auto [event_start, event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_start, 3); // Event starts after BOM
  EXPECT_EQ(event_end, 15);  // BOM (3) + "data: hello\n" (12) = 15
  EXPECT_EQ(next_event, 16); // BOM (3) + "data: hello\n\n" (13) = 16

  // Verify the event content using the API-provided positions
  auto event_str = absl::string_view(buffer).substr(event_start, event_end - event_start);
  auto event = SseParser::parseEvent(event_str);
  ASSERT_TRUE(event.data.has_value());
  EXPECT_EQ(event.data.value(), "hello");
}

} // namespace
} // namespace Sse
} // namespace Http
} // namespace Envoy
