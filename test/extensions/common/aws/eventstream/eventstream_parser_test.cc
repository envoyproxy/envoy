#include <zlib.h>

#include "source/extensions/common/aws/eventstream/eventstream_parser.h"

#include "absl/strings/match.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {
namespace Eventstream {
namespace {

// Test helper: compute CRC32 using zlib directly (since EventstreamParser::computeCrc32 is private)
uint32_t testComputeCrc32(absl::string_view data, uint32_t initial_crc = 0) {
  return crc32(initial_crc, reinterpret_cast<const Bytef*>(data.data()),
               static_cast<uInt>(data.size()));
}

// Helper to create a valid eventstream message
std::string createEventstreamMessage(const std::string& headers_data, const std::string& payload) {
  const uint32_t headers_length = headers_data.size();
  const uint32_t total_length = PRELUDE_SIZE + headers_length + payload.size() + TRAILER_SIZE;

  std::vector<uint8_t> message(total_length);

  // Write total_length (big-endian)
  message[0] = (total_length >> 24) & 0xFF;
  message[1] = (total_length >> 16) & 0xFF;
  message[2] = (total_length >> 8) & 0xFF;
  message[3] = total_length & 0xFF;

  // Write headers_length (big-endian)
  message[4] = (headers_length >> 24) & 0xFF;
  message[5] = (headers_length >> 16) & 0xFF;
  message[6] = (headers_length >> 8) & 0xFF;
  message[7] = headers_length & 0xFF;

  // Compute and write prelude_crc
  uint32_t prelude_crc =
      testComputeCrc32(absl::string_view(reinterpret_cast<char*>(message.data()), 8));
  message[8] = (prelude_crc >> 24) & 0xFF;
  message[9] = (prelude_crc >> 16) & 0xFF;
  message[10] = (prelude_crc >> 8) & 0xFF;
  message[11] = prelude_crc & 0xFF;

  // Copy headers
  std::copy(headers_data.begin(), headers_data.end(), message.begin() + 12);

  // Copy payload
  std::copy(payload.begin(), payload.end(), message.begin() + 12 + headers_length);

  // Compute and write message_crc (covers everything except the last 4 bytes)
  uint32_t message_crc = testComputeCrc32(
      absl::string_view(reinterpret_cast<char*>(message.data()), total_length - 4));
  message[total_length - 4] = (message_crc >> 24) & 0xFF;
  message[total_length - 3] = (message_crc >> 16) & 0xFF;
  message[total_length - 2] = (message_crc >> 8) & 0xFF;
  message[total_length - 1] = message_crc & 0xFF;

  return std::string(reinterpret_cast<char*>(message.data()), total_length);
}

class EventstreamParserTest : public testing::Test {};

// Test parseMessage with buffer too small for prelude
TEST_F(EventstreamParserTest, ParseMessageTooSmall) {
  const std::string buffer(10, '\0'); // Less than 12 bytes (prelude size)
  auto result = EventstreamParser::parseMessage(buffer);
  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(result->message.has_value());
  EXPECT_EQ(result->bytes_consumed, 0);
}

// Test parseMessage with incomplete message (have prelude, waiting for rest)
TEST_F(EventstreamParserTest, ParseMessageIncomplete) {
  // Create a prelude indicating 100 byte message, but only provide 50 bytes
  uint8_t buffer[50] = {0};
  // total_length = 100 (big-endian)
  buffer[0] = 0;
  buffer[1] = 0;
  buffer[2] = 0;
  buffer[3] = 100;
  // headers_length = 0
  buffer[4] = 0;
  buffer[5] = 0;
  buffer[6] = 0;
  buffer[7] = 0;
  // prelude_crc - compute it
  uint32_t prelude_crc = testComputeCrc32(absl::string_view(reinterpret_cast<char*>(buffer), 8));
  buffer[8] = (prelude_crc >> 24) & 0xFF;
  buffer[9] = (prelude_crc >> 16) & 0xFF;
  buffer[10] = (prelude_crc >> 8) & 0xFF;
  buffer[11] = prelude_crc & 0xFF;

  auto result =
      EventstreamParser::parseMessage(absl::string_view(reinterpret_cast<char*>(buffer), 50));
  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(result->message.has_value());
  EXPECT_EQ(result->bytes_consumed, 0);
}

// Test parseMessage with invalid prelude CRC
TEST_F(EventstreamParserTest, ParseMessageBadPreludeCrc) {
  uint8_t buffer[16] = {0};
  buffer[0] = 0;
  buffer[1] = 0;
  buffer[2] = 0;
  buffer[3] = 16;
  buffer[4] = 0;
  buffer[5] = 0;
  buffer[6] = 0;
  buffer[7] = 0;
  // Invalid prelude_crc
  buffer[8] = 0xFF;
  buffer[9] = 0xFF;
  buffer[10] = 0xFF;
  buffer[11] = 0xFF;
  buffer[12] = 0;
  buffer[13] = 0;
  buffer[14] = 0;
  buffer[15] = 0;

  auto result =
      EventstreamParser::parseMessage(absl::string_view(reinterpret_cast<char*>(buffer), 16));
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(absl::StrContains(result.status().message(), "Prelude CRC"));
}

// Test parseMessage with no headers and no payload
TEST_F(EventstreamParserTest, ParseMessageMinimal) {
  std::string msg = createEventstreamMessage("", "");
  auto result = EventstreamParser::parseMessage(msg);
  ASSERT_TRUE(result.ok()) << result.status().message();
  ASSERT_TRUE(result->message.has_value());
  EXPECT_TRUE(result->message->headers.empty());
  EXPECT_TRUE(result->message->payload_bytes.empty());
  EXPECT_EQ(result->bytes_consumed, msg.size());
}

// Test parseMessage with payload only
TEST_F(EventstreamParserTest, ParseMessageWithPayload) {
  std::string payload = R"({"type":"message_delta","usage":{"output_tokens":42}})";
  std::string msg = createEventstreamMessage("", payload);
  auto result = EventstreamParser::parseMessage(msg);
  ASSERT_TRUE(result.ok()) << result.status().message();
  ASSERT_TRUE(result->message.has_value());
  EXPECT_TRUE(result->message->headers.empty());
  EXPECT_EQ(result->message->payload_bytes, payload);
  EXPECT_EQ(result->bytes_consumed, msg.size());
}

// Test parseMessage with headers and payload
TEST_F(EventstreamParserTest, ParseMessageWithHeadersAndPayload) {
  // Create header bytes for ":message-type" = "event" (string)
  std::vector<uint8_t> header_bytes;
  std::string header_name = ":message-type";
  header_bytes.push_back(static_cast<uint8_t>(header_name.size()));
  for (char c : header_name)
    header_bytes.push_back(c);
  header_bytes.push_back(7); // type = string
  std::string header_value = "event";
  header_bytes.push_back(0);
  header_bytes.push_back(static_cast<uint8_t>(header_value.size()));
  for (char c : header_value)
    header_bytes.push_back(c);

  std::string headers_data(reinterpret_cast<char*>(header_bytes.data()), header_bytes.size());
  std::string payload = "test payload";

  std::string msg = createEventstreamMessage(headers_data, payload);
  auto result = EventstreamParser::parseMessage(msg);
  ASSERT_TRUE(result.ok()) << result.status().message();
  ASSERT_TRUE(result->message.has_value());
  ASSERT_EQ(result->message->headers.size(), 1);
  EXPECT_EQ(result->message->headers[0].name, ":message-type");
  EXPECT_EQ(absl::get<std::string>(result->message->headers[0].value.value), "event");
  EXPECT_EQ(result->message->payload_bytes, payload);
  EXPECT_EQ(result->bytes_consumed, msg.size());
}

// Test parseMessage with invalid message CRC
TEST_F(EventstreamParserTest, ParseMessageBadMessageCrc) {
  std::string msg = createEventstreamMessage("", "payload");
  // Corrupt the last byte (message CRC)
  msg[msg.size() - 1] ^= 0xFF;

  auto result = EventstreamParser::parseMessage(msg);
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(absl::StrContains(result.status().message(), "Message CRC"));
}

// Test all header value types via parseMessage
TEST_F(EventstreamParserTest, ParseMessageAllHeaderTypes) {
  std::vector<uint8_t> header_bytes;

  // BoolTrue header
  header_bytes.push_back(2);
  header_bytes.push_back('b');
  header_bytes.push_back('t');
  header_bytes.push_back(0); // BoolTrue

  // BoolFalse header
  header_bytes.push_back(2);
  header_bytes.push_back('b');
  header_bytes.push_back('f');
  header_bytes.push_back(1); // BoolFalse

  // Byte header (signed: -2 = 0xFE)
  header_bytes.push_back(1);
  header_bytes.push_back('b');
  header_bytes.push_back(2); // Byte
  header_bytes.push_back(0xFE);

  // Short header
  header_bytes.push_back(1);
  header_bytes.push_back('s');
  header_bytes.push_back(3); // Short
  header_bytes.push_back(0x12);
  header_bytes.push_back(0x34);

  // Int64 header
  header_bytes.push_back(1);
  header_bytes.push_back('l');
  header_bytes.push_back(5); // Int64
  header_bytes.push_back(0x00);
  header_bytes.push_back(0x00);
  header_bytes.push_back(0x00);
  header_bytes.push_back(0x00);
  header_bytes.push_back(0x00);
  header_bytes.push_back(0x00);
  header_bytes.push_back(0x00);
  header_bytes.push_back(0x01);

  // ByteArray header
  header_bytes.push_back(2);
  header_bytes.push_back('b');
  header_bytes.push_back('a');
  header_bytes.push_back(6); // ByteArray
  header_bytes.push_back(0);
  header_bytes.push_back(2); // length = 2
  header_bytes.push_back(0xDE);
  header_bytes.push_back(0xAD);

  // Timestamp header
  header_bytes.push_back(2);
  header_bytes.push_back('t');
  header_bytes.push_back('s');
  header_bytes.push_back(8); // Timestamp
  // Value: 1000 milliseconds
  header_bytes.push_back(0);
  header_bytes.push_back(0);
  header_bytes.push_back(0);
  header_bytes.push_back(0);
  header_bytes.push_back(0);
  header_bytes.push_back(0);
  header_bytes.push_back(0x03);
  header_bytes.push_back(0xE8);

  // UUID header
  header_bytes.push_back(1);
  header_bytes.push_back('u');
  header_bytes.push_back(9); // UUID
  for (int i = 0; i < 16; i++)
    header_bytes.push_back(i);

  std::string headers_data(reinterpret_cast<char*>(header_bytes.data()), header_bytes.size());
  std::string msg = createEventstreamMessage(headers_data, "");
  auto result = EventstreamParser::parseMessage(msg);
  ASSERT_TRUE(result.ok()) << result.status().message();
  ASSERT_TRUE(result->message.has_value());
  const auto& headers = result->message->headers;
  ASSERT_EQ(headers.size(), 8);

  // Verify each header
  EXPECT_EQ(headers[0].name, "bt");
  EXPECT_EQ(headers[0].value.type, HeaderValueType::BoolTrue);

  EXPECT_EQ(headers[1].name, "bf");
  EXPECT_EQ(headers[1].value.type, HeaderValueType::BoolFalse);

  EXPECT_EQ(headers[2].name, "b");
  EXPECT_EQ(headers[2].value.type, HeaderValueType::Byte);
  EXPECT_EQ(absl::get<int8_t>(headers[2].value.value), -2);

  EXPECT_EQ(headers[3].name, "s");
  EXPECT_EQ(headers[3].value.type, HeaderValueType::Short);
  EXPECT_EQ(absl::get<int16_t>(headers[3].value.value), 0x1234);

  EXPECT_EQ(headers[4].name, "l");
  EXPECT_EQ(headers[4].value.type, HeaderValueType::Int64);
  EXPECT_EQ(absl::get<int64_t>(headers[4].value.value), 1);

  EXPECT_EQ(headers[5].name, "ba");
  EXPECT_EQ(headers[5].value.type, HeaderValueType::ByteArray);
  EXPECT_EQ(absl::get<std::string>(headers[5].value.value).size(), 2);

  EXPECT_EQ(headers[6].name, "ts");
  EXPECT_EQ(headers[6].value.type, HeaderValueType::Timestamp);
  EXPECT_EQ(absl::get<int64_t>(headers[6].value.value), 1000);

  EXPECT_EQ(headers[7].name, "u");
  EXPECT_EQ(headers[7].value.type, HeaderValueType::Uuid);
}

// Test parseMessage with multiple messages in buffer
TEST_F(EventstreamParserTest, ParseMessageMultipleMessages) {
  std::string msg1 = createEventstreamMessage("", "first");
  std::string msg2 = createEventstreamMessage("", "second");
  std::string buffer = msg1 + msg2;

  // Parse first message
  auto result1 = EventstreamParser::parseMessage(buffer);
  ASSERT_TRUE(result1.ok());
  ASSERT_TRUE(result1->message.has_value());
  EXPECT_EQ(result1->message->payload_bytes, "first");
  EXPECT_EQ(result1->bytes_consumed, msg1.size());

  // Parse remaining buffer
  absl::string_view remaining = absl::string_view(buffer).substr(result1->bytes_consumed);
  auto result2 = EventstreamParser::parseMessage(remaining);
  ASSERT_TRUE(result2.ok());
  ASSERT_TRUE(result2->message.has_value());
  EXPECT_EQ(result2->message->payload_bytes, "second");
  EXPECT_EQ(result2->bytes_consumed, msg2.size());
}

// Test payload exceeds maximum size (24 MB)
TEST_F(EventstreamParserTest, ParseMessagePayloadExceedsMax) {
  uint8_t buffer[16] = {0};
  // total_length = MAX_PAYLOAD_SIZE + PRELUDE_SIZE + TRAILER_SIZE + 1 (payload 1 byte over limit)
  uint32_t total = MAX_PAYLOAD_SIZE + PRELUDE_SIZE + TRAILER_SIZE + 1;
  buffer[0] = (total >> 24) & 0xFF;
  buffer[1] = (total >> 16) & 0xFF;
  buffer[2] = (total >> 8) & 0xFF;
  buffer[3] = total & 0xFF;
  // headers_length = 0
  buffer[4] = 0;
  buffer[5] = 0;
  buffer[6] = 0;
  buffer[7] = 0;
  // prelude_crc
  uint32_t prelude_crc = testComputeCrc32(absl::string_view(reinterpret_cast<char*>(buffer), 8));
  buffer[8] = (prelude_crc >> 24) & 0xFF;
  buffer[9] = (prelude_crc >> 16) & 0xFF;
  buffer[10] = (prelude_crc >> 8) & 0xFF;
  buffer[11] = prelude_crc & 0xFF;

  auto result =
      EventstreamParser::parseMessage(absl::string_view(reinterpret_cast<char*>(buffer), 16));
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(absl::StrContains(result.status().message(), "Payload exceeds maximum"));
}

// Test total_length smaller than minimum
TEST_F(EventstreamParserTest, ParseMessageTotalLengthTooSmall) {
  uint8_t buffer[16] = {0};
  // total_length = 15 (less than MIN_MESSAGE_SIZE of 16)
  buffer[0] = 0;
  buffer[1] = 0;
  buffer[2] = 0;
  buffer[3] = 15;
  // headers_length = 0
  buffer[4] = 0;
  buffer[5] = 0;
  buffer[6] = 0;
  buffer[7] = 0;
  // prelude_crc
  uint32_t prelude_crc = testComputeCrc32(absl::string_view(reinterpret_cast<char*>(buffer), 8));
  buffer[8] = (prelude_crc >> 24) & 0xFF;
  buffer[9] = (prelude_crc >> 16) & 0xFF;
  buffer[10] = (prelude_crc >> 8) & 0xFF;
  buffer[11] = prelude_crc & 0xFF;

  auto result =
      EventstreamParser::parseMessage(absl::string_view(reinterpret_cast<char*>(buffer), 16));
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(absl::StrContains(result.status().message(), "Invalid message length"));
}

// Test headers_length exceeds available space
TEST_F(EventstreamParserTest, ParseMessageHeadersLengthExceedsMessage) {
  uint8_t buffer[16] = {0};
  // total_length = 16 (minimum)
  buffer[0] = 0;
  buffer[1] = 0;
  buffer[2] = 0;
  buffer[3] = 16;
  // headers_length = 10 (exceeds total_length - PRELUDE_SIZE - TRAILER_SIZE = 0)
  buffer[4] = 0;
  buffer[5] = 0;
  buffer[6] = 0;
  buffer[7] = 10;
  // prelude_crc
  uint32_t prelude_crc = testComputeCrc32(absl::string_view(reinterpret_cast<char*>(buffer), 8));
  buffer[8] = (prelude_crc >> 24) & 0xFF;
  buffer[9] = (prelude_crc >> 16) & 0xFF;
  buffer[10] = (prelude_crc >> 8) & 0xFF;
  buffer[11] = prelude_crc & 0xFF;

  auto result =
      EventstreamParser::parseMessage(absl::string_view(reinterpret_cast<char*>(buffer), 16));
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(absl::StrContains(result.status().message(), "Headers length exceeds message"));
}

// Test headers_length exceeds maximum allowed
TEST_F(EventstreamParserTest, ParseMessageHeadersLengthExceedsMax) {
  uint8_t buffer[16] = {0};
  // total_length = large enough
  uint32_t total = MAX_HEADERS_SIZE + PRELUDE_SIZE + TRAILER_SIZE + 100;
  buffer[0] = (total >> 24) & 0xFF;
  buffer[1] = (total >> 16) & 0xFF;
  buffer[2] = (total >> 8) & 0xFF;
  buffer[3] = total & 0xFF;
  // headers_length = MAX_HEADERS_SIZE + 1
  uint32_t headers_len = MAX_HEADERS_SIZE + 1;
  buffer[4] = (headers_len >> 24) & 0xFF;
  buffer[5] = (headers_len >> 16) & 0xFF;
  buffer[6] = (headers_len >> 8) & 0xFF;
  buffer[7] = headers_len & 0xFF;
  // prelude_crc
  uint32_t prelude_crc = testComputeCrc32(absl::string_view(reinterpret_cast<char*>(buffer), 8));
  buffer[8] = (prelude_crc >> 24) & 0xFF;
  buffer[9] = (prelude_crc >> 16) & 0xFF;
  buffer[10] = (prelude_crc >> 8) & 0xFF;
  buffer[11] = prelude_crc & 0xFF;

  auto result =
      EventstreamParser::parseMessage(absl::string_view(reinterpret_cast<char*>(buffer), 16));
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(absl::StrContains(result.status().message(), "Headers length exceeds maximum"));
}

// Test with AWS Bedrock-like payload (realistic JSON)
TEST_F(EventstreamParserTest, ParseMessageBedrockLikePayload) {
  std::string payload =
      R"({"type":"message_delta","delta":{"stop_reason":"end_turn"},"usage":{"output_tokens":156}})";
  std::string msg = createEventstreamMessage("", payload);

  auto result = EventstreamParser::parseMessage(msg);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->message.has_value());
  EXPECT_EQ(result->message->payload_bytes, payload);
}

// Test header with name_length = 0
TEST_F(EventstreamParserTest, ParseMessageHeaderNameLengthZero) {
  std::vector<uint8_t> header_bytes;
  header_bytes.push_back(0); // name_length = 0 (invalid)

  std::string headers_data(reinterpret_cast<char*>(header_bytes.data()), header_bytes.size());
  std::string msg = createEventstreamMessage(headers_data, "");

  auto result = EventstreamParser::parseMessage(msg);
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(absl::StrContains(result.status().message(), "Invalid header name length"));
}

// Test header with unknown type
TEST_F(EventstreamParserTest, ParseMessageHeaderUnknownType) {
  std::vector<uint8_t> header_bytes;
  header_bytes.push_back(1);   // name_length = 1
  header_bytes.push_back('x'); // name = "x"
  header_bytes.push_back(10);  // type = 10 (invalid, max is 9)

  std::string headers_data(reinterpret_cast<char*>(header_bytes.data()), header_bytes.size());
  std::string msg = createEventstreamMessage(headers_data, "");

  auto result = EventstreamParser::parseMessage(msg);
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(absl::StrContains(result.status().message(), "Unknown header value type"));
}

// Test Int32 header type
TEST_F(EventstreamParserTest, ParseMessageHeaderInt32) {
  std::vector<uint8_t> header_bytes;
  header_bytes.push_back(1);   // name_length = 1
  header_bytes.push_back('i'); // name = "i"
  header_bytes.push_back(4);   // type = Int32
  // Value: 0x12345678
  header_bytes.push_back(0x12);
  header_bytes.push_back(0x34);
  header_bytes.push_back(0x56);
  header_bytes.push_back(0x78);

  std::string headers_data(reinterpret_cast<char*>(header_bytes.data()), header_bytes.size());
  std::string msg = createEventstreamMessage(headers_data, "");

  auto result = EventstreamParser::parseMessage(msg);
  ASSERT_TRUE(result.ok()) << result.status().message();
  ASSERT_TRUE(result->message.has_value());
  ASSERT_EQ(result->message->headers.size(), 1);
  EXPECT_EQ(result->message->headers[0].name, "i");
  EXPECT_EQ(result->message->headers[0].value.type, HeaderValueType::Int32);
  EXPECT_EQ(absl::get<int32_t>(result->message->headers[0].value.value), 0x12345678);
}

// Test header truncation: missing name or type
TEST_F(EventstreamParserTest, ParseMessageHeaderTruncatedName) {
  std::vector<uint8_t> header_bytes;
  header_bytes.push_back(5); // name_length = 5, but only provide 2 chars
  header_bytes.push_back('a');
  header_bytes.push_back('b');

  std::string headers_data(reinterpret_cast<char*>(header_bytes.data()), header_bytes.size());
  std::string msg = createEventstreamMessage(headers_data, "");

  auto result = EventstreamParser::parseMessage(msg);
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(absl::StrContains(result.status().message(), "Header truncated"));
}

// Test header truncation: missing byte value
TEST_F(EventstreamParserTest, ParseMessageHeaderTruncatedByteValue) {
  std::vector<uint8_t> header_bytes;
  header_bytes.push_back(1);   // name_length = 1
  header_bytes.push_back('b'); // name = "b"
  header_bytes.push_back(2);   // type = Byte
  // Missing: the actual byte value

  std::string headers_data(reinterpret_cast<char*>(header_bytes.data()), header_bytes.size());
  std::string msg = createEventstreamMessage(headers_data, "");

  auto result = EventstreamParser::parseMessage(msg);
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(absl::StrContains(result.status().message(), "Header truncated"));
}

// Test header truncation: missing short value
TEST_F(EventstreamParserTest, ParseMessageHeaderTruncatedShortValue) {
  std::vector<uint8_t> header_bytes;
  header_bytes.push_back(1);    // name_length = 1
  header_bytes.push_back('s');  // name = "s"
  header_bytes.push_back(3);    // type = Short
  header_bytes.push_back(0x12); // Only 1 byte, need 2

  std::string headers_data(reinterpret_cast<char*>(header_bytes.data()), header_bytes.size());
  std::string msg = createEventstreamMessage(headers_data, "");

  auto result = EventstreamParser::parseMessage(msg);
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(absl::StrContains(result.status().message(), "Header truncated"));
}

// Test header truncation: missing int32 value
TEST_F(EventstreamParserTest, ParseMessageHeaderTruncatedInt32Value) {
  std::vector<uint8_t> header_bytes;
  header_bytes.push_back(1);   // name_length = 1
  header_bytes.push_back('i'); // name = "i"
  header_bytes.push_back(4);   // type = Int32
  header_bytes.push_back(0x12);
  header_bytes.push_back(0x34); // Only 2 bytes, need 4

  std::string headers_data(reinterpret_cast<char*>(header_bytes.data()), header_bytes.size());
  std::string msg = createEventstreamMessage(headers_data, "");

  auto result = EventstreamParser::parseMessage(msg);
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(absl::StrContains(result.status().message(), "Header truncated"));
}

// Test header truncation: missing int64 value
TEST_F(EventstreamParserTest, ParseMessageHeaderTruncatedInt64Value) {
  std::vector<uint8_t> header_bytes;
  header_bytes.push_back(1);   // name_length = 1
  header_bytes.push_back('l'); // name = "l"
  header_bytes.push_back(5);   // type = Int64
  header_bytes.push_back(0x12);
  header_bytes.push_back(0x34);
  header_bytes.push_back(0x56);
  header_bytes.push_back(0x78); // Only 4 bytes, need 8

  std::string headers_data(reinterpret_cast<char*>(header_bytes.data()), header_bytes.size());
  std::string msg = createEventstreamMessage(headers_data, "");

  auto result = EventstreamParser::parseMessage(msg);
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(absl::StrContains(result.status().message(), "Header truncated"));
}

// Test header truncation: missing string length
TEST_F(EventstreamParserTest, ParseMessageHeaderTruncatedStringLength) {
  std::vector<uint8_t> header_bytes;
  header_bytes.push_back(1);   // name_length = 1
  header_bytes.push_back('s'); // name = "s"
  header_bytes.push_back(7);   // type = String
  header_bytes.push_back(0);   // Only 1 byte of length, need 2

  std::string headers_data(reinterpret_cast<char*>(header_bytes.data()), header_bytes.size());
  std::string msg = createEventstreamMessage(headers_data, "");

  auto result = EventstreamParser::parseMessage(msg);
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(absl::StrContains(result.status().message(), "Header truncated"));
}

// Test header truncation: missing string data
TEST_F(EventstreamParserTest, ParseMessageHeaderTruncatedStringData) {
  std::vector<uint8_t> header_bytes;
  header_bytes.push_back(1);   // name_length = 1
  header_bytes.push_back('s'); // name = "s"
  header_bytes.push_back(7);   // type = String
  header_bytes.push_back(0);
  header_bytes.push_back(10); // length = 10
  header_bytes.push_back('a');
  header_bytes.push_back('b'); // Only 2 bytes, need 10

  std::string headers_data(reinterpret_cast<char*>(header_bytes.data()), header_bytes.size());
  std::string msg = createEventstreamMessage(headers_data, "");

  auto result = EventstreamParser::parseMessage(msg);
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(absl::StrContains(result.status().message(), "Header truncated"));
}

// Test header truncation: missing uuid value
TEST_F(EventstreamParserTest, ParseMessageHeaderTruncatedUuidValue) {
  std::vector<uint8_t> header_bytes;
  header_bytes.push_back(1);   // name_length = 1
  header_bytes.push_back('u'); // name = "u"
  header_bytes.push_back(9);   // type = UUID
  for (int i = 0; i < 8; i++) {
    header_bytes.push_back(i); // Only 8 bytes, need 16
  }

  std::string headers_data(reinterpret_cast<char*>(header_bytes.data()), header_bytes.size());
  std::string msg = createEventstreamMessage(headers_data, "");

  auto result = EventstreamParser::parseMessage(msg);
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(absl::StrContains(result.status().message(), "Header truncated"));
}

// Test header value too long (> MAX_HEADER_VALUE_LENGTH)
TEST_F(EventstreamParserTest, ParseMessageHeaderValueTooLong) {
  std::vector<uint8_t> header_bytes;
  header_bytes.push_back(1);   // name_length = 1
  header_bytes.push_back('s'); // name = "s"
  header_bytes.push_back(7);   // type = String
  // length = MAX_HEADER_VALUE_LENGTH + 1 = 32768
  header_bytes.push_back(0x80);
  header_bytes.push_back(0x00);

  std::string headers_data(reinterpret_cast<char*>(header_bytes.data()), header_bytes.size());
  std::string msg = createEventstreamMessage(headers_data, "");

  auto result = EventstreamParser::parseMessage(msg);
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(absl::StrContains(result.status().message(), "Header value too long"));
}

// Test string with length 0 (allowed for interoperability)
TEST_F(EventstreamParserTest, ParseMessageHeaderStringLengthZero) {
  std::vector<uint8_t> header_bytes;
  header_bytes.push_back(1);   // name_length = 1
  header_bytes.push_back('s'); // name = "s"
  header_bytes.push_back(7);   // type = String
  header_bytes.push_back(0);
  header_bytes.push_back(0); // length = 0

  std::string headers_data(reinterpret_cast<char*>(header_bytes.data()), header_bytes.size());
  std::string msg = createEventstreamMessage(headers_data, "");

  auto result = EventstreamParser::parseMessage(msg);
  ASSERT_TRUE(result.ok()) << result.status().message();
  ASSERT_TRUE(result->message.has_value());
  ASSERT_EQ(result->message->headers.size(), 1);
  EXPECT_EQ(result->message->headers[0].name, "s");
  EXPECT_EQ(absl::get<std::string>(result->message->headers[0].value.value), "");
}

// Test byte_array with length 0 (allowed for interoperability)
TEST_F(EventstreamParserTest, ParseMessageHeaderByteArrayLengthZero) {
  std::vector<uint8_t> header_bytes;
  header_bytes.push_back(1);   // name_length = 1
  header_bytes.push_back('b'); // name = "b"
  header_bytes.push_back(6);   // type = ByteArray
  header_bytes.push_back(0);
  header_bytes.push_back(0); // length = 0

  std::string headers_data(reinterpret_cast<char*>(header_bytes.data()), header_bytes.size());
  std::string msg = createEventstreamMessage(headers_data, "");

  auto result = EventstreamParser::parseMessage(msg);
  ASSERT_TRUE(result.ok()) << result.status().message();
  ASSERT_TRUE(result->message.has_value());
  ASSERT_EQ(result->message->headers.size(), 1);
  EXPECT_EQ(result->message->headers[0].name, "b");
  EXPECT_EQ(absl::get<std::string>(result->message->headers[0].value.value), "");
}

// Test duplicate header names
TEST_F(EventstreamParserTest, ParseMessageDuplicateHeaderNames) {
  std::vector<uint8_t> header_bytes;

  // First header: "x" = true
  header_bytes.push_back(1);
  header_bytes.push_back('x');
  header_bytes.push_back(0); // BoolTrue

  // Duplicate header: "x" = false
  header_bytes.push_back(1);
  header_bytes.push_back('x');
  header_bytes.push_back(1); // BoolFalse

  std::string headers_data(reinterpret_cast<char*>(header_bytes.data()), header_bytes.size());
  std::string msg = createEventstreamMessage(headers_data, "");

  auto result = EventstreamParser::parseMessage(msg);
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(absl::StrContains(result.status().message(), "Duplicate header name"));
}

// Test CRC errors return DataLoss status code
TEST_F(EventstreamParserTest, ParseMessageCrcErrorsReturnDataLoss) {
  // Bad prelude CRC
  {
    uint8_t buffer[16] = {0};
    buffer[3] = 16;
    buffer[8] = 0xFF;
    buffer[9] = 0xFF;
    buffer[10] = 0xFF;
    buffer[11] = 0xFF;

    auto result =
        EventstreamParser::parseMessage(absl::string_view(reinterpret_cast<char*>(buffer), 16));
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kDataLoss);
  }

  // Bad message CRC
  {
    std::string msg = createEventstreamMessage("", "payload");
    msg[msg.size() - 1] ^= 0xFF;

    auto result = EventstreamParser::parseMessage(msg);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kDataLoss);
  }
}

} // namespace
} // namespace Eventstream
} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
