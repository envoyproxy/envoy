#include "contrib/postgres_inspector/filters/listener/source/postgres_message_parser.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace PostgresInspector {

bool PostgresMessageParser::isSslRequest(const Buffer::Instance& buffer, uint64_t offset) {
  // SSL request is exactly 8 bytes: length (4 bytes) + code (4 bytes).
  if (buffer.length() < offset + SSL_REQUEST_MESSAGE_SIZE) {
    return false;
  }

  const uint32_t length = buffer.peekBEInt<uint32_t>(offset);
  if (length != SSL_REQUEST_MESSAGE_SIZE) {
    return false;
  }

  const uint32_t code = buffer.peekBEInt<uint32_t>(offset + 4);
  return code == SSL_REQUEST_CODE;
}

bool PostgresMessageParser::isCancelRequest(const Buffer::Instance& buffer, uint64_t offset) {
  if (buffer.length() < offset + CANCEL_REQUEST_MESSAGE_SIZE) {
    return false;
  }
  const uint32_t length = buffer.peekBEInt<uint32_t>(offset);
  if (length != CANCEL_REQUEST_MESSAGE_SIZE) {
    return false;
  }
  const uint32_t code = buffer.peekBEInt<uint32_t>(offset + 4);
  return code == CANCEL_REQUEST_CODE;
}

bool PostgresMessageParser::parseStartupMessage(const Buffer::Instance& buffer, uint64_t offset,
                                                StartupMessage& message,
                                                uint32_t max_message_size) {
  message.reset();

  // Need at least 8 bytes for length + version.
  if (buffer.length() < offset + STARTUP_HEADER_SIZE) {
    return false;
  }

  // Read message length (includes itself).
  message.length = buffer.peekBEInt<uint32_t>(offset);

  // Validate message length.
  if (message.length < STARTUP_HEADER_SIZE || message.length > max_message_size) {
    return false;
  }

  // Check if we have the complete message.
  if (buffer.length() < offset + message.length) {
    return false;
  }

  // Read protocol version.
  message.protocol_version = buffer.peekBEInt<uint32_t>(offset + 4);

  // Parse parameters (null-terminated key-value pairs).
  uint64_t pos = offset + STARTUP_HEADER_SIZE;
  const uint64_t end = offset + message.length;

  while (pos < end - 1) { // -1 for final null terminator
    std::string key, value;

    if (!readCString(buffer, pos, key) || pos > end) {
      return false;
    }

    if (key.empty()) {
      break; // Final null terminator reached
    }

    if (!readCString(buffer, pos, value) || pos > end) {
      return false;
    }

    message.parameters[key] = value;
  }

  return true;
}

bool PostgresMessageParser::hasCompleteMessage(const Buffer::Instance& buffer, uint64_t offset,
                                               uint32_t& message_length) {
  if (buffer.length() < offset + 4) {
    return false;
  }

  message_length = buffer.peekBEInt<uint32_t>(offset);
  return buffer.length() >= offset + message_length;
}

bool PostgresMessageParser::readCString(const Buffer::Instance& buffer, uint64_t& offset,
                                        std::string& result) {
  result.clear();

  while (offset < buffer.length()) {
    const char c = buffer.peekInt<char>(offset);
    offset++;

    if (c == '\0') {
      return true;
    }

    result += c;
  }

  return false; // Reached end without null terminator
}

} // namespace PostgresInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
