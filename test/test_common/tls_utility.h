#pragma once

#include <string>
#include <vector>

namespace Envoy {
namespace Tls {
namespace Test {

/**
 * Generate a TLS ClientHello in wire-format.
 * @param sni_name The name to include as a Server Name Indication. No
 *                 SNI extension is added if sni_name is empty.
 */
std::vector<uint8_t> generateClientHello(const std::string& sni_name);

} // namespace Test
} // namespace Tls
} // namespace Envoy
