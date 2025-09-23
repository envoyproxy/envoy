#pragma once

#include <map>
#include <string>

#include "source/common/buffer/buffer_impl.h"

#include "contrib/postgres/protocol/postgres_protocol.h"
#include "contrib/postgres_inspector/filters/listener/source/postgres_message_parser.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace PostgresInspector {

// Protocol constants are imported via postgres_message_parser.h which includes the shared
// postgres_protocol.h. We use the constants from that header to ensure consistency.

/**
 * Test utilities for PostgreSQL protocol messages.
 */
class PostgresTestUtils {
public:
  /**
   * Create an SSL request message.
   * Format: Int32(SSL_REQUEST_MESSAGE_SIZE) + Int32(SSL_REQUEST_CODE).
   * @return buffer containing SSL request.
   */
  static Buffer::OwnedImpl createSslRequest() {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<uint32_t>(SSL_REQUEST_MESSAGE_SIZE);
    buffer.writeBEInt<uint32_t>(SSL_REQUEST_CODE);
    return buffer;
  }

  /**
   * Create a startup message with the given parameters.
   * Format: Int32(length) + Int32(POSTGRES_PROTOCOL_VERSION) + null-terminated parameter pairs +
   * final null.
   * @param params map of parameter name -> value pairs.
   * @return buffer containing startup message.
   */
  static Buffer::OwnedImpl createStartupMessage(const std::map<std::string, std::string>& params) {
    Buffer::OwnedImpl buffer;

    // Calculate message length.
    uint32_t length = STARTUP_HEADER_SIZE; // length field + version field
    for (const auto& [key, value] : params) {
      length += key.length() + 1 + value.length() + 1; // +1 for null terminators
    }
    length += 1; // Final null terminator

    buffer.writeBEInt<uint32_t>(length);
    buffer.writeBEInt<uint32_t>(POSTGRES_PROTOCOL_VERSION);

    // Write parameters.
    for (const auto& [key, value] : params) {
      buffer.add(key.c_str(), key.length() + 1);     // Include null terminator
      buffer.add(value.c_str(), value.length() + 1); // Include null terminator
    }

    // Final null terminator.
    buffer.writeByte(0);

    return buffer;
  }

  /**
   * Create a startup message with invalid protocol version.
   * @param version the invalid version to use.
   * @return buffer containing invalid startup message.
   */
  static Buffer::OwnedImpl createInvalidStartupMessage(uint32_t version) {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<uint32_t>(STARTUP_HEADER_SIZE);
    buffer.writeBEInt<uint32_t>(version); // Invalid version
    return buffer;
  }

  /**
   * Create a partial startup message (incomplete).
   * @param total_length the declared length of the message
   * @param actual_bytes the number of bytes to actually include
   * @return buffer containing partial message
   */
  static Buffer::OwnedImpl createPartialStartupMessage(uint32_t total_length,
                                                       uint32_t actual_bytes) {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<uint32_t>(total_length);
    buffer.writeBEInt<uint32_t>(POSTGRES_PROTOCOL_VERSION);

    // Add some dummy data up to actual_bytes (minus the header bytes already added).
    const uint32_t remaining =
        (actual_bytes > STARTUP_HEADER_SIZE) ? actual_bytes - STARTUP_HEADER_SIZE : 0;
    for (uint32_t i = 0; i < remaining; ++i) {
      buffer.writeByte('x');
    }

    return buffer;
  }

  /**
   * Create an oversized startup message.
   * @param size the size to make the message
   * @return buffer containing oversized message
   */
  static Buffer::OwnedImpl createOversizedMessage(uint32_t size) {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<uint32_t>(size);
    buffer.writeBEInt<uint32_t>(POSTGRES_PROTOCOL_VERSION);

    // Fill with dummy data.
    for (uint32_t i = STARTUP_HEADER_SIZE; i < size; ++i) {
      buffer.writeByte('x');
    }

    return buffer;
  }

  /**
   * Create a CancelRequest message.
   * Format: Int32(16) + Int32(80877102) + Int32(pid) + Int32(secret).
   */
  static Buffer::OwnedImpl createCancelRequest(uint32_t pid, uint32_t secret) {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<uint32_t>(CANCEL_REQUEST_MESSAGE_SIZE);
    buffer.writeBEInt<uint32_t>(CANCEL_REQUEST_CODE);
    buffer.writeBEInt<uint32_t>(pid);
    buffer.writeBEInt<uint32_t>(secret);
    return buffer;
  }

  /**
   * Create random non-PostgreSQL data.
   * @param size the size of data to create
   * @return buffer containing random data
   */
  static Buffer::OwnedImpl createRandomData(size_t size) {
    Buffer::OwnedImpl buffer;
    for (size_t i = 0; i < size; ++i) {
      buffer.writeByte(static_cast<char>(i % 256));
    }
    return buffer;
  }
};

} // namespace PostgresInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
