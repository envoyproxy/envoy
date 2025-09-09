#pragma once

#include <string>

#include "absl/strings/string_view.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace TlsInspector {

/**
 * Utility class for creating ``JA4`` fingerprints from TLS ClientHello messages.
 * ``JA4`` is an improved version of ``JA3`` that includes TLS version, ciphers, extensions,
 * and ALPN information in a hex format.
 *
 * Format: `tXXdYYZZ_CIPHERHASH_EXTENSIONHASH`
 *
 * Where:
 * - ``t`` = protocol type (t for TLS, q for QUIC, d for ``DTLS``)
 * - ``XX`` = TLS version (13, 12, etc.)
 * - ``d/i`` = SNI presence (d = domain present, i = no SNI)
 * - ``YY`` = number of cipher suites (2 digits)
 * - ``ZZ`` = number of extensions (2 digits)
 * - ``_CIPHERHASH`` = first 12 hex chars of SHA-256 hash of cipher suites
 * - ``_EXTENSIONHASH`` = first 12 hex chars of SHA-256 hash of extensions
 *
 * See: https://github.com/FoxIO-LLC/ja4/blob/main/technical_details/JA4.md
 */
class JA4Fingerprinter {
public:
  /**
   * Creates a ``JA4`` fingerprint from a TLS ClientHello message.
   * @param ssl_client_hello The SSL ClientHello message
   * @return ``JA4`` fingerprint string
   */
  static std::string create(const SSL_CLIENT_HELLO* ssl_client_hello);

  /**
   * Checks if a value is not a GREASE value (RFC 8701)
   * @param id The value to check
   * @return true if the value is NOT a GREASE value, false otherwise
   */
  static bool isNotGrease(uint16_t id);

private:
  // Constants
  static constexpr size_t JA4_HASH_LENGTH = 12;

  /**
   * Maps a TLS version uint16_t to its string representation.
   * @param version The TLS version value
   * @return String representation of the TLS version
   */
  static absl::string_view mapVersionToString(uint16_t version);

  /**
   * Gets the highest supported TLS version from the ClientHello.
   * @param ssl_client_hello The SSL ClientHello message
   * @return String representation of the TLS version
   */
  static absl::string_view getJA4TlsVersion(const SSL_CLIENT_HELLO* ssl_client_hello);

  /**
   * Checks if SNI is present in the ClientHello.
   * @param ssl_client_hello The SSL ClientHello message
   * @return true if SNI is present, false otherwise
   */
  static bool hasSNI(const SSL_CLIENT_HELLO* ssl_client_hello);

  /**
   * Counts the number of cipher suites in the ClientHello.
   * @param ssl_client_hello The SSL ClientHello message
   * @return Number of cipher suites
   */
  static uint32_t countCiphers(const SSL_CLIENT_HELLO* ssl_client_hello);

  /**
   * Counts the number of extensions in the ClientHello.
   * @param ssl_client_hello The SSL ClientHello message
   * @return Number of extensions
   */
  static uint32_t countExtensions(const SSL_CLIENT_HELLO* ssl_client_hello);

  /**
   * Formats a value as a two-digit string.
   * @param value The value to format
   * @return Two-digit string representation of the value
   */
  static std::string formatTwoDigits(uint32_t value);

  /**
   * Gets the ALPN characters for the ``JA4`` fingerprint.
   * @param ssl_client_hello The SSL ClientHello message
   * @return ALPN character string
   */
  static std::string getJA4AlpnChars(const SSL_CLIENT_HELLO* ssl_client_hello);

  /**
   * Creates a hash of the cipher suites for the ``JA4`` fingerprint.
   * @param ssl_client_hello The SSL ClientHello message
   * @return Hash of the cipher suites
   */
  static std::string getJA4CipherHash(const SSL_CLIENT_HELLO* ssl_client_hello);

  /**
   * Creates a hash of the extensions for the ``JA4`` fingerprint.
   * @param ssl_client_hello The SSL ClientHello message
   * @return Hash of the extensions
   */
  static std::string getJA4ExtensionHash(const SSL_CLIENT_HELLO* ssl_client_hello);
};

} // namespace TlsInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
