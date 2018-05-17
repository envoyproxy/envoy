#pragma once

#include <string>
#include <vector>

namespace Envoy {
namespace Tls {
namespace Test {

/**
 * Generate a TLS ClientHello in wire-format.
 * @param sni_name The name to include as a Server Name Indication.
 *                 No SNI extension is added if sni_name is empty.
 * @param alpn Protocol(s) list in the wire-format (i.e. 8-bit length-prefixed string) to advertise
 *             in Application-Layer Protocol Negotiation. No ALPN is advertised if alpn is empty.
 */
std::vector<uint8_t> generateClientHello(const std::string& sni_name, const std::string& alpn);

} // namespace Test
} // namespace Tls
} // namespace Envoy
