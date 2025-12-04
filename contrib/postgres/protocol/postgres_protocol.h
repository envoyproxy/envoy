#pragma once

#include <cstdint>

namespace Envoy {
namespace Postgres {
namespace Protocol {

/**
 * Shared PostgreSQL protocol constants.
 * Used by both postgres_proxy (network filter) and postgres_inspector (listener filter).
 *
 * References:
 * - PostgreSQL Protocol Documentation:
 *   https://www.postgresql.org/docs/current/protocol-message-formats.html
 * - PostgreSQL Protocol Flow:
 *   https://www.postgresql.org/docs/current/protocol-flow.html
 */

// Protocol version 3.0 = 0x00030000 (hex) = 196608 (decimal).
// This is the current and most widely used PostgreSQL protocol version.
constexpr uint32_t POSTGRES_PROTOCOL_VERSION_30 = 0x00030000;

// SSL request code = 0x04d2162f (hex) = 80877103 (decimal).
// Sent by client to request SSL/TLS encryption before sending startup message.
// Format: Int32(8) + Int32(SSL_REQUEST_CODE).
constexpr uint32_t SSL_REQUEST_CODE = 0x04d2162f;

// Cancel request code = 0x04d2162e (hex) = 80877102 (decimal).
// Sent by client on a NEW connection to cancel a long-running query on another connection.
// Format: Int32(16) + Int32(CANCEL_REQUEST_CODE) + Int32(process_id) + Int32(secret_key).
// Note: CancelRequest is always unencrypted, even if the original connection used SSL.
constexpr uint32_t CANCEL_REQUEST_CODE = 0x04d2162e;

// GSSAPI encryption request code = 0x04d21630 (hex) = 80877104 (decimal).
// Similar to SSL request but for GSSAPI encryption.
constexpr uint32_t GSSAPI_ENC_REQUEST_CODE = 0x04d21630;

// Message size constants.
constexpr uint32_t STARTUP_HEADER_SIZE = 8;             // Length(4) + Version/Code(4).
constexpr uint32_t SSL_REQUEST_MESSAGE_SIZE = 8;        // Length(4) + Code(4).
constexpr uint32_t CANCEL_REQUEST_MESSAGE_SIZE = 16;    // Length(4) + Code(4) + PID(4) + Key(4).
constexpr uint32_t GSSAPI_ENC_REQUEST_MESSAGE_SIZE = 8; // Length(4) + Code(4).

// Base code for encryption requests (most significant 16 bits = 1234).
// Used to identify messages that negotiate encryption: SSL, GSSAPI, or future variants.
constexpr uint32_t ENCRYPTION_REQUEST_BASE_CODE = 0x04d20000;

// Maximum startup packet length (10000 bytes).
// Defined in PostgreSQL source code as MAX_STARTUP_PACKET_LENGTH.
// Reference: https://github.com/postgres/postgres/search?q=MAX_STARTUP_PACKET_LENGTH
constexpr uint64_t MAX_STARTUP_PACKET_LENGTH = 10000;

} // namespace Protocol
} // namespace Postgres
} // namespace Envoy
