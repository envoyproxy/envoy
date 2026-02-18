#include "source/extensions/common/aws/eventstream/eventstream_parser.h"

#include <zlib.h>

#include "source/common/common/safe_memcpy.h"

#include "absl/base/internal/endian.h"
#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {
namespace Eventstream {

absl::StatusOr<ParseResult> EventstreamParser::parseMessage(absl::string_view buffer) {
  // Need at least the prelude to read total_length and validate prelude CRC
  if (buffer.size() < PRELUDE_SIZE) {
    return ParseResult{absl::nullopt, 0};
  }

  const uint8_t* data = reinterpret_cast<const uint8_t*>(buffer.data());

  // Read prelude fields
  const uint32_t total_length = absl::big_endian::Load32(data + TOTAL_LENGTH_OFFSET);
  const uint32_t headers_length = absl::big_endian::Load32(data + HEADERS_LENGTH_OFFSET);
  const uint32_t prelude_crc = absl::big_endian::Load32(data + PRELUDE_CRC_OFFSET);

  // Validate total_length bounds to prevent unbounded buffering
  if (total_length < MIN_MESSAGE_SIZE) {
    return absl::InvalidArgumentError("Invalid message length");
  }
  if (total_length > MAX_TOTAL_LENGTH) {
    return absl::ResourceExhaustedError("Message length exceeds maximum");
  }

  // Validate headers_length doesn't exceed message size
  if (headers_length > total_length - PRELUDE_SIZE - TRAILER_SIZE) {
    return absl::InvalidArgumentError("Headers length exceeds message size");
  }

  if (headers_length > MAX_HEADERS_SIZE) {
    return absl::ResourceExhaustedError("Headers length exceeds maximum");
  }

  // Validate payload doesn't exceed maximum (24 MB)
  const uint32_t payload_length = total_length - PRELUDE_SIZE - headers_length - TRAILER_SIZE;
  if (payload_length > MAX_PAYLOAD_SIZE) {
    return absl::ResourceExhaustedError("Payload exceeds maximum size");
  }

  // Verify prelude CRC (covers first 8 bytes: total_length + headers_length)
  const uint32_t computed_prelude_crc = computeCrc32(buffer.substr(0, PRELUDE_CRC_OFFSET));
  if (computed_prelude_crc != prelude_crc) {
    return absl::DataLossError("Prelude CRC mismatch");
  }

  // Check if we have the complete message
  if (buffer.size() < total_length) {
    return ParseResult{absl::nullopt, 0};
  }

  // Verify message CRC (covers everything except the last 4 bytes)
  const uint32_t message_crc = absl::big_endian::Load32(data + total_length - TRAILER_SIZE);
  const uint32_t computed_message_crc = computeCrc32(buffer.substr(0, total_length - TRAILER_SIZE));
  if (computed_message_crc != message_crc) {
    return absl::DataLossError("Message CRC mismatch");
  }

  // Parse headers
  absl::string_view headers_bytes = buffer.substr(PRELUDE_SIZE, headers_length);
  auto headers_result = parseHeaders(headers_bytes);
  if (!headers_result.ok()) {
    return headers_result.status();
  }

  // Extract payload
  absl::string_view payload_data = buffer.substr(PRELUDE_SIZE + headers_length, payload_length);

  ParsedMessage parsed;
  parsed.headers = std::move(headers_result.value());
  parsed.payload_bytes = std::string(payload_data);

  return ParseResult{std::move(parsed), total_length};
}

absl::StatusOr<std::vector<Header>>
EventstreamParser::parseHeaders(absl::string_view headers_bytes) {
  std::vector<Header> headers;

  if (headers_bytes.empty()) {
    return headers;
  }

  const uint8_t* data = reinterpret_cast<const uint8_t*>(headers_bytes.data());
  size_t remaining = headers_bytes.size();

  while (remaining > 0) {
    const uint8_t name_length = data[0];
    if (name_length == 0) {
      return absl::InvalidArgumentError("Invalid header name length");
    }

    // Need name_length bytes + 1 byte for type
    if (remaining < NAME_LENGTH_SIZE + name_length + TYPE_SIZE) {
      return absl::InvalidArgumentError("Header truncated: missing name or type");
    }

    Header header;
    header.name = std::string(reinterpret_cast<const char*>(data + NAME_LENGTH_SIZE), name_length);

    const uint8_t type_byte = data[NAME_LENGTH_SIZE + name_length];

    header.value.type = static_cast<HeaderValueType>(type_byte);
    const size_t value_offset = NAME_LENGTH_SIZE + name_length + TYPE_SIZE;
    size_t bytes_consumed = 0;

    switch (header.value.type) {
    case HeaderValueType::BoolTrue:
      header.value.value = true;
      bytes_consumed = value_offset;
      break;

    case HeaderValueType::BoolFalse:
      header.value.value = false;
      bytes_consumed = value_offset;
      break;

    case HeaderValueType::Byte:
      if (remaining < value_offset + BYTE_VALUE_SIZE) {
        return absl::InvalidArgumentError("Header truncated: missing byte value");
      }
      header.value.value = static_cast<int8_t>(data[value_offset]);
      bytes_consumed = value_offset + BYTE_VALUE_SIZE;
      break;

    case HeaderValueType::Short:
      if (remaining < value_offset + SHORT_VALUE_SIZE) {
        return absl::InvalidArgumentError("Header truncated: missing short value");
      }
      header.value.value = static_cast<int16_t>(absl::big_endian::Load16(data + value_offset));
      bytes_consumed = value_offset + SHORT_VALUE_SIZE;
      break;

    case HeaderValueType::Int32:
      if (remaining < value_offset + INT32_VALUE_SIZE) {
        return absl::InvalidArgumentError("Header truncated: missing int32 value");
      }
      header.value.value = static_cast<int32_t>(absl::big_endian::Load32(data + value_offset));
      bytes_consumed = value_offset + INT32_VALUE_SIZE;
      break;

    case HeaderValueType::Int64:
    case HeaderValueType::Timestamp:
      if (remaining < value_offset + INT64_VALUE_SIZE) {
        return absl::InvalidArgumentError("Header truncated: missing int64/timestamp value");
      }
      header.value.value = static_cast<int64_t>(absl::big_endian::Load64(data + value_offset));
      bytes_consumed = value_offset + INT64_VALUE_SIZE;
      break;

    case HeaderValueType::ByteArray:
    case HeaderValueType::String: {
      if (remaining < value_offset + STRING_LENGTH_SIZE) {
        return absl::InvalidArgumentError("Header truncated: missing string/bytes length");
      }
      const uint16_t value_length = absl::big_endian::Load16(data + value_offset);
      if (value_length == 0) {
        return absl::InvalidArgumentError("Header string/bytes value must not be empty");
      }
      if (value_length > MAX_HEADER_STRING_LENGTH) {
        return absl::InvalidArgumentError("Header value too long");
      }
      if (remaining < value_offset + STRING_LENGTH_SIZE + value_length) {
        return absl::InvalidArgumentError("Header truncated: missing string/bytes data");
      }
      header.value.value = std::string(
          reinterpret_cast<const char*>(data + value_offset + STRING_LENGTH_SIZE), value_length);
      bytes_consumed = value_offset + STRING_LENGTH_SIZE + value_length;
      break;
    }

    case HeaderValueType::Uuid: {
      if (remaining < value_offset + UUID_VALUE_SIZE) {
        return absl::InvalidArgumentError("Header truncated: missing uuid value");
      }
      std::array<uint8_t, 16> uuid;
      // Copies sizeof(uuid) == 16 bytes.
      safeMemcpyUnsafeSrc(&uuid, data + value_offset);
      header.value.value = uuid;
      bytes_consumed = value_offset + UUID_VALUE_SIZE;
      break;
    }

    default:
      return absl::InvalidArgumentError("Unknown header value type");
    }

    headers.push_back(std::move(header));
    data += bytes_consumed;
    remaining -= bytes_consumed;
  }

  return headers;
}

uint32_t EventstreamParser::computeCrc32(absl::string_view data, uint32_t initial_crc) {
  return crc32(initial_crc, reinterpret_cast<const Bytef*>(data.data()),
               static_cast<uInt>(data.size()));
}

} // namespace Eventstream
} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
