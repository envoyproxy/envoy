#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Envoy {
namespace Tls {
namespace Test {

/**
 * Generate a TLS ClientHello in wire-format.
 * @param tls_min_version Minimum supported TLS version to advertise.
 * @param tls_max_version Maximum supported TLS version to advertise.
 * @param sni_name The name to include as a Server Name Indication.
 *                 No SNI extension is added if sni_name is empty.
 * @param alpn Protocol(s) list in the wire-format (i.e. 8-bit length-prefixed string) to advertise
 *             in Application-Layer Protocol Negotiation. No ALPN is advertised if alpn is empty.
 */
std::vector<uint8_t> generateClientHello(uint16_t tls_min_version, uint16_t tls_max_version,
                                         const std::string& sni_name, const std::string& alpn);

/**
 * Generate a TLS ClientHello in wire-format from a `JA3` fingerprint.
 * @param ja3_fingerprint The `JA3` fingerprint to use when creating the ClientHello message.
 */
std::vector<uint8_t> generateClientHelloFromJA3Fingerprint(const std::string& ja3_fingerprint,
                                                           size_t target_size = 0);

/**
 * Generate a TLS ClientHello in wire-format without any extensions.
 * This creates a minimal, valid ClientHello message with only the required fields.
 *
 * @param tls_max_version Maximum supported TLS version to advertise.
 * @return A vector containing the wire-format ClientHello message bytes.
 */
std::vector<uint8_t> generateClientHelloWithoutExtensions(uint16_t tls_max_version);

/**
 * Generate a TLS ClientHello in wire-format with empty extensions.
 * This creates a minimal, valid ClientHello message with only the required fields.
 *
 * @param tls_max_version Maximum supported TLS version to advertise.
 * @return A vector containing the wire-format ClientHello message bytes.
 */
std::vector<uint8_t> generateClientHelloEmptyExtensions(uint16_t tls_max_version);

} // namespace Test
} // namespace Tls
} // namespace Envoy
