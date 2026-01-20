#include "source/common/http/sse/sse_parser.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {
namespace Sse {
namespace {

class SseParserTest : public testing::Test {};

// Test extractDataField with single data field
TEST_F(SseParserTest, ExtractDataFieldSingle) {
  const std::string event = "data: hello world\n";
  EXPECT_EQ(SseParser::extractDataField(event), "hello world");
}

// Test extractDataField with multiple data fields
TEST_F(SseParserTest, ExtractDataFieldMultiple) {
  const std::string event = "data: first line\ndata: second line\n";
  EXPECT_EQ(SseParser::extractDataField(event), "first line\nsecond line");
}

// Test extractDataField with no data field
TEST_F(SseParserTest, ExtractDataFieldNone) {
  const std::string event = "event: ping\nid: 123\n";
  EXPECT_EQ(SseParser::extractDataField(event), "");
}

// Test extractDataField with empty data field
TEST_F(SseParserTest, ExtractDataFieldEmpty) {
  const std::string event = "data:\n";
  EXPECT_EQ(SseParser::extractDataField(event), "");
}

// Test extractDataField with data field without space after colon
TEST_F(SseParserTest, ExtractDataFieldNoSpace) {
  const std::string event = "data:nospace\n";
  EXPECT_EQ(SseParser::extractDataField(event), "nospace");
}

// Test extractDataField with comment lines
TEST_F(SseParserTest, ExtractDataFieldWithComments) {
  const std::string event = ": comment line\ndata: actual data\n: another comment\n";
  EXPECT_EQ(SseParser::extractDataField(event), "actual data");
}

// Test extractDataField with mixed fields
TEST_F(SseParserTest, ExtractDataFieldMixed) {
  const std::string event = "event: message\ndata: content\nid: 42\ndata: more content\n";
  EXPECT_EQ(SseParser::extractDataField(event), "content\nmore content");
}

// Test extractDataField with CRLF line endings
TEST_F(SseParserTest, ExtractDataFieldCRLF) {
  const std::string event = "data: test\r\n";
  EXPECT_EQ(SseParser::extractDataField(event), "test");
}

// Test extractDataField with CR line endings
TEST_F(SseParserTest, ExtractDataFieldCR) {
  const std::string event = "data: test\r";
  EXPECT_EQ(SseParser::extractDataField(event), "test");
}

// Test findEventEnd with complete event (double newline)
TEST_F(SseParserTest, FindEventEndComplete) {
  const std::string buffer = "data: test\n\nmore data";
  auto [event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_end, 11);  // Position before the second newline
  EXPECT_EQ(next_event, 12); // Position after the second newline
}

// Test findEventEnd with incomplete event
TEST_F(SseParserTest, FindEventEndIncomplete) {
  const std::string buffer = "data: test\n";
  auto [event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_end, absl::string_view::npos);
  EXPECT_EQ(next_event, absl::string_view::npos);
}

// Test findEventEnd with end_stream and incomplete event
TEST_F(SseParserTest, FindEventEndEndStream) {
  const std::string buffer = "data: test\n";
  auto [event_end, next_event] = SseParser::findEventEnd(buffer, true);
  // With end_stream, incomplete event should not be found (still needs blank line)
  EXPECT_EQ(event_end, absl::string_view::npos);
  EXPECT_EQ(next_event, absl::string_view::npos);
}

// Test findEventEnd with end_stream and blank line
TEST_F(SseParserTest, FindEventEndEndStreamWithBlankLine) {
  const std::string buffer = "data: test\n\n";
  auto [event_end, next_event] = SseParser::findEventEnd(buffer, true);
  EXPECT_EQ(event_end, 11);
  EXPECT_EQ(next_event, 12);
}

// Test findEventEnd with CRLF blank line
TEST_F(SseParserTest, FindEventEndCRLF) {
  const std::string buffer = "data: test\r\n\r\n";
  auto [event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_end, 12);
  EXPECT_EQ(next_event, 14);
}

// Test findEventEnd with mixed line endings
TEST_F(SseParserTest, FindEventEndMixed) {
  const std::string buffer = "data: test\r\n\n";
  auto [event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_end, 12);
  EXPECT_EQ(next_event, 13);
}

// Test findEventEnd with CR line endings
TEST_F(SseParserTest, FindEventEndCR) {
  const std::string buffer = "data: test\r\rmore";
  auto [event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_end, 11);
  EXPECT_EQ(next_event, 12);
}

// Test findEventEnd with multiple events
TEST_F(SseParserTest, FindEventEndMultiple) {
  const std::string buffer = "data: first\n\ndata: second\n\n";
  auto [event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_end, 12);
  EXPECT_EQ(next_event, 13);

  // Find second event
  auto [event_end2, next_event2] = SseParser::findEventEnd(buffer.substr(next_event), false);
  EXPECT_EQ(event_end2, 13);
  EXPECT_EQ(next_event2, 14);
}

// Test findEventEnd with empty buffer
TEST_F(SseParserTest, FindEventEndEmpty) {
  const std::string buffer = "";
  auto [event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_end, absl::string_view::npos);
  EXPECT_EQ(next_event, absl::string_view::npos);
}

// Test findEventEnd with only blank lines
TEST_F(SseParserTest, FindEventEndOnlyBlankLines) {
  const std::string buffer = "\n\n";
  auto [event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_end, 0); // Empty event
  EXPECT_EQ(next_event, 1);
}

// Test findEventEnd with comment before blank line
TEST_F(SseParserTest, FindEventEndWithComment) {
  const std::string buffer = ": comment\ndata: test\n\n";
  auto [event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_end, 21);
  EXPECT_EQ(next_event, 22);
}

// Test findEventEnd with trailing CR needing CRLF check
TEST_F(SseParserTest, FindEventEndTrailingCR) {
  const std::string buffer = "data: test\r";
  auto [event_end, next_event] = SseParser::findEventEnd(buffer, false);
  // Should wait for potential LF
  EXPECT_EQ(event_end, absl::string_view::npos);
  EXPECT_EQ(next_event, absl::string_view::npos);
}

// Test findEventEnd with trailing CR and end_stream
TEST_F(SseParserTest, FindEventEndTrailingCREndStream) {
  const std::string buffer = "data: test\r";
  auto [event_end, next_event] = SseParser::findEventEnd(buffer, true);
  // With end_stream, should treat CR as line ending but still need blank line
  EXPECT_EQ(event_end, absl::string_view::npos);
  EXPECT_EQ(next_event, absl::string_view::npos);
}

// Test extractDataField with JSON content
TEST_F(SseParserTest, ExtractDataFieldJSON) {
  const std::string event = "data: {\"key\":\"value\"}\n";
  EXPECT_EQ(SseParser::extractDataField(event), "{\"key\":\"value\"}");
}

// Test extractDataField with multiline JSON (multiple data fields)
TEST_F(SseParserTest, ExtractDataFieldMultilineJSON) {
  const std::string event = "data: {\"start\":\n"
                            "data: \"middle\",\n"
                            "data: \"end\":true}\n";
  EXPECT_EQ(SseParser::extractDataField(event), "{\"start\":\n\"middle\",\n\"end\":true}");
}

// Test field line with colon in value
TEST_F(SseParserTest, ExtractDataFieldColonInValue) {
  const std::string event = "data: http://example.com\n";
  EXPECT_EQ(SseParser::extractDataField(event), "http://example.com");
}

// Test field line without colon
TEST_F(SseParserTest, ExtractDataFieldNoColon) {
  const std::string event = "dataonly\n";
  EXPECT_EQ(SseParser::extractDataField(event), "");
}

// Test with very long data field
TEST_F(SseParserTest, ExtractDataFieldLong) {
  std::string long_data(10000, 'x');
  const std::string event = "data: " + long_data + "\n";
  EXPECT_EQ(SseParser::extractDataField(event), long_data);
}

// Test extractDataField with Unicode
TEST_F(SseParserTest, ExtractDataFieldUnicode) {
  const std::string event = "data: Hello 世界 🌍\n";
  EXPECT_EQ(SseParser::extractDataField(event), "Hello 世界 🌍");
}

// Test extractDataField with null bytes
TEST_F(SseParserTest, ExtractDataFieldNullBytes) {
  const std::string event = std::string("data: hello\0world\n", 18);
  const std::string result = SseParser::extractDataField(event);
  EXPECT_EQ(result.size(), 11);
  EXPECT_EQ(result, std::string("hello\0world", 11));
}

// Test extractDataField with data field followed by whitespace
TEST_F(SseParserTest, ExtractDataFieldTrailingSpace) {
  const std::string event = "data: value \n";
  EXPECT_EQ(SseParser::extractDataField(event), "value ");
}

// Test extractDataField with multiple spaces after colon
TEST_F(SseParserTest, ExtractDataFieldMultipleSpaces) {
  const std::string event = "data:  extra spaces\n";
  EXPECT_EQ(SseParser::extractDataField(event), " extra spaces");
}

// Test extractDataField with tab after colon
// Per SSE spec, only space character (not tab) is stripped
TEST_F(SseParserTest, ExtractDataFieldTab) {
  const std::string event = "data:\tvalue\n";
  EXPECT_EQ(SseParser::extractDataField(event), "\tvalue");
}

// Test findEventEnd with three consecutive blank lines
TEST_F(SseParserTest, FindEventEndTripleBlankLines) {
  const std::string buffer = "data: test\n\n\n";
  auto [event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_end, 11);
  EXPECT_EQ(next_event, 12);
}

// Test findEventEnd with buffer starting with blank line
TEST_F(SseParserTest, FindEventEndStartsWithBlankLine) {
  const std::string buffer = "\ndata: test\n\n";
  auto [event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_end, 0); // Empty event before the blank line
  EXPECT_EQ(next_event, 1);
}

// Test findEventEnd with CRLF then LF
TEST_F(SseParserTest, FindEventEndCRLFThenLF) {
  const std::string buffer = "data: test\r\n\n";
  auto [event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_end, 12);
  EXPECT_EQ(next_event, 13);
}

// Test findEventEnd with LF then CRLF
TEST_F(SseParserTest, FindEventEndLFThenCRLF) {
  const std::string buffer = "data: test\n\r\n";
  auto [event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_end, 11);
  EXPECT_EQ(next_event, 13);
}

// Test findEventEnd with CR, CR (double CR)
TEST_F(SseParserTest, FindEventEndDoubleCR) {
  const std::string buffer = "data: test\r\rmore";
  auto [event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_end, 11);
  EXPECT_EQ(next_event, 12);
}

// Test extractDataField with field name containing whitespace
TEST_F(SseParserTest, ExtractDataFieldWhitespaceInFieldName) {
  const std::string event = "data extra: value\n";
  // Per SSE spec, field name is until first colon, so "data extra" is the field name
  EXPECT_EQ(SseParser::extractDataField(event), "");
}

// Test extractDataField with only colon
TEST_F(SseParserTest, ExtractDataFieldOnlyColon) {
  const std::string event = ":\n";
  EXPECT_EQ(SseParser::extractDataField(event), "");
}

// Test extractDataField with data field mixed with event field
TEST_F(SseParserTest, ExtractDataFieldWithEventField) {
  const std::string event = "event: custom\ndata: value\n";
  EXPECT_EQ(SseParser::extractDataField(event), "value");
}

// Test findEventEnd with very long line
TEST_F(SseParserTest, FindEventEndLongLine) {
  std::string buffer = "data: " + std::string(10000, 'x') + "\n\n";
  auto [event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_end, 10007);
  EXPECT_EQ(next_event, 10008);
}

// Test extractDataField with multiple data fields separated by other fields
TEST_F(SseParserTest, ExtractDataFieldInterspersed) {
  const std::string event = "data: first\nid: 123\ndata: second\nevent: msg\ndata: third\n";
  EXPECT_EQ(SseParser::extractDataField(event), "first\nsecond\nthird");
}

// Test findEventEnd with only comments
TEST_F(SseParserTest, FindEventEndOnlyComments) {
  const std::string buffer = ": comment 1\n: comment 2\n\n";
  auto [event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_end, 24);
  EXPECT_EQ(next_event, 25);
}

// Test extractDataField with empty string
TEST_F(SseParserTest, ExtractDataFieldEmptyString) {
  const std::string event = "";
  EXPECT_EQ(SseParser::extractDataField(event), "");
}

// Test findEventEnd with single newline at end
TEST_F(SseParserTest, FindEventEndSingleNewlineAtEnd) {
  const std::string buffer = "data: test\n";
  auto [event_end, next_event] = SseParser::findEventEnd(buffer, false);
  EXPECT_EQ(event_end, absl::string_view::npos);
  EXPECT_EQ(next_event, absl::string_view::npos);
}

// Test findEventEnd with trailing CR followed by more data
TEST_F(SseParserTest, FindEventEndTrailingCRWithData) {
  const std::string buffer = "data: test\rmore data";
  auto [event_end, next_event] = SseParser::findEventEnd(buffer, false);
  // Should find CR as line ending
  EXPECT_EQ(event_end, absl::string_view::npos);
}

// Test extractDataField with empty lines to exercise parseFieldLine empty line case
TEST_F(SseParserTest, ExtractDataFieldWithEmptyLines) {
  const std::string event = "\ndata: test\n\n";
  EXPECT_EQ(SseParser::extractDataField(event), "test");
}

// Test extractDataField with no line ending (exercises findLineEnd with end_stream=true)
TEST_F(SseParserTest, ExtractDataFieldNoLineEnding) {
  const std::string event = "data: test";
  EXPECT_EQ(SseParser::extractDataField(event), "test");
}

// Test findEventEnd with data without newline and end_stream=true
TEST_F(SseParserTest, FindEventEndNoLineEndingEndStream) {
  const std::string buffer = "data: test";
  auto [event_end, next_event] = SseParser::findEventEnd(buffer, true);

  EXPECT_EQ(event_end, absl::string_view::npos);
  EXPECT_EQ(next_event, absl::string_view::npos);
}

} // namespace
} // namespace Sse
} // namespace Http
} // namespace Envoy
