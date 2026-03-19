#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {
namespace Eventstream {

// AWS Eventstream protocol constants.
// Reference: https://smithy.io/2.0/aws/amazon-eventstream.html

// Message structure sizes.
constexpr uint32_t PRELUDE_SIZE = 12; // total_length(4) + headers_length(4) + prelude_crc(4)
constexpr uint32_t TRAILER_SIZE = 4;  // message_crc(4)
constexpr uint32_t MIN_MESSAGE_SIZE = PRELUDE_SIZE + TRAILER_SIZE; // 16 bytes minimum
constexpr uint32_t MAX_PAYLOAD_SIZE = 24 * 1024 * 1024;            // 24 MB
constexpr uint32_t MAX_HEADERS_SIZE = 128 * 1024;                  // 128 KB
// Upper bound on the total_length wire field to prevent unbounded buffering. Even with a valid
// prelude CRC (which is not a MAC), an attacker could craft a message with an absurd total_length.
constexpr uint32_t MAX_TOTAL_LENGTH =
    PRELUDE_SIZE + MAX_HEADERS_SIZE + MAX_PAYLOAD_SIZE + TRAILER_SIZE;
constexpr uint16_t MAX_HEADER_STRING_LENGTH = 32767; // 2^15 - 1

// Prelude field offsets.
constexpr size_t TOTAL_LENGTH_OFFSET = 0;
constexpr size_t HEADERS_LENGTH_OFFSET = 4;
constexpr size_t PRELUDE_CRC_OFFSET = 8;

// Header field sizes.
constexpr size_t NAME_LENGTH_SIZE = 1;   // 1 byte for header name length
constexpr size_t TYPE_SIZE = 1;          // 1 byte for header value type
constexpr size_t STRING_LENGTH_SIZE = 2; // 2 bytes for string/bytearray length prefix

// Value type sizes.
constexpr size_t BYTE_VALUE_SIZE = 1;
constexpr size_t SHORT_VALUE_SIZE = 2;
constexpr size_t INT32_VALUE_SIZE = 4;
constexpr size_t INT64_VALUE_SIZE = 8;
constexpr size_t UUID_VALUE_SIZE = 16;

/**
 * Header value types as defined by the AWS eventstream specification.
 */
enum class HeaderValueType : uint8_t {
  BoolTrue = 0,
  BoolFalse = 1,
  Byte = 2,
  Short = 3,
  Int32 = 4,
  Int64 = 5,
  ByteArray = 6,
  String = 7,
  Timestamp = 8,
  Uuid = 9,
};

/**
 * Represents a parsed header value.
 * Uses absl::variant to hold the appropriate type based on HeaderValueType.
 */
struct HeaderValue {
  HeaderValueType type;
  absl::variant<bool,                   // BoolTrue, BoolFalse
                int8_t,                 // Byte
                int16_t,                // Short
                int32_t,                // Int32
                int64_t,                // Int64, Timestamp
                std::string,            // ByteArray, String
                std::array<uint8_t, 16> // Uuid
                >
      value;
};

/**
 * Represents a single header (name-value pair).
 */
struct Header {
  std::string name;
  HeaderValue value;
};

/**
 * Represents a fully parsed eventstream message.
 */
struct ParsedMessage {
  std::vector<Header> headers;
  std::string payload_bytes; // Arbitrary bytes; not necessarily UTF-8.
};

/**
 * Result of attempting to parse a message from a buffer.
 * Single-pass design: returns either a parsed message, indication of incomplete data, or error.
 */
struct ParseResult {
  // The parsed message, if a complete message was found.
  // nullopt if the buffer doesn't contain a complete message yet.
  absl::optional<ParsedMessage> message;

  // Number of bytes consumed from the buffer.
  // 0 if incomplete (need more data).
  // > 0 if a message was parsed (remove this many bytes from buffer).
  size_t bytes_consumed;
};

/**
 * Parser for AWS Eventstream binary protocol.
 * Implements the specification: https://smithy.io/2.0/aws/amazon-eventstream.html
 *
 * Example usage:
 *   absl::string_view remaining = buffer;
 *
 *   while (true) {
 *     auto result = EventstreamParser::parseMessage(remaining);
 *     if (!result.ok()) {
 *       // Handle error (corrupt data)
 *       break;
 *     }
 *     if (!result->message.has_value()) {
 *       // Incomplete - wait for more data
 *       break;
 *     }
 *     // Process result->message->headers and result->message->payload_bytes
 *     remaining.remove_prefix(result->bytes_consumed);
 *   }
 */
class EventstreamParser {
public:
  /**
   * Attempts to parse an eventstream message from the buffer.
   * Single-pass design: checks for completeness and parses in one call.
   * Validates both prelude CRC and message CRC.
   *
   * @param buffer contiguous bytes containing incoming data (may be incomplete).
   *               Callers should ensure the buffer is linearized before passing.
   * @return ParseResult with message if complete, nullopt if incomplete, or error status.
   */
  static absl::StatusOr<ParseResult> parseMessage(absl::string_view buffer);

private:
  /**
   * Parses the headers section of an eventstream message.
   *
   * @param headers_bytes the headers section bytes.
   * @return vector of Header on success, or error status on failure.
   */
  static absl::StatusOr<std::vector<Header>> parseHeaders(absl::string_view headers_bytes);

  /**
   * Computes CRC32 checksum using the same algorithm as AWS eventstream.
   * Uses zlib's crc32() function.
   *
   * @param data the data to compute checksum for.
   * @param initial_crc the initial CRC value (0 for first computation).
   * @return the computed CRC32 value.
   */
  static uint32_t computeCrc32(absl::string_view data, uint32_t initial_crc = 0);
};

} // namespace Eventstream
} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
