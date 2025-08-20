#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/platform.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace PostgresInspector {

// PostgreSQL protocol constants.
constexpr uint32_t POSTGRES_PROTOCOL_VERSION = 196608; // Protocol version 3.0
constexpr uint32_t SSL_REQUEST_CODE = 80877103;        // Magic number for SSL request

/**
 * Parsed PostgreSQL startup message.
 */
struct StartupMessage {
  uint32_t length{0};
  uint32_t protocol_version{0};
  std::map<std::string, std::string> parameters;

  /**
   * Reset the startup message to initial state.
   */
  void reset() {
    length = 0;
    protocol_version = 0;
    parameters.clear();
  }
};

/**
 * PostgreSQL message parser utility class.
 */
class PostgresMessageParser {
public:
  /**
   * Check if buffer contains an SSL request at the given offset.
   * SSL request format: Int32(8) + Int32(80877103)
   * @param buffer the buffer to examine
   * @param offset the offset in the buffer to start checking
   * @return true if SSL request found, false otherwise
   */
  static bool isSslRequest(const Buffer::Instance& buffer, uint64_t offset);

  /**
   * Parse a startup message from the buffer.
   * Startup message format: Int32(length) + Int32(version) + null-terminated parameter pairs
   * @param buffer the buffer containing message data
   * @param offset the offset in the buffer to start parsing
   * @param message the startup message structure to populate
   * @param max_message_size the maximum allowed message size
   * @return true if successful, false if more data needed or error
   */
  static bool parseStartupMessage(const Buffer::Instance& buffer, uint64_t offset,
                                  StartupMessage& message, uint32_t max_message_size);

  /**
   * Check if buffer contains a complete message starting at offset.
   * @param buffer the buffer to examine
   * @param offset the offset in the buffer to start checking
   * @param message_length set to the message length if a complete message is found
   * @return true if complete message found, false if more data needed
   */
  static bool hasCompleteMessage(const Buffer::Instance& buffer, uint64_t offset,
                                 uint32_t& message_length);

private:
  /**
   * Helper to read a null-terminated string from buffer.
   * @param buffer the buffer to read from
   * @param offset the current offset, will be advanced on success
   * @param result the string to populate
   * @return true if successful, false if incomplete
   */
  static bool readCString(const Buffer::Instance& buffer, uint64_t& offset, std::string& result);
};

} // namespace PostgresInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
