#pragma once

namespace Envoy {
namespace Ssl {

/**
 * Returns FIPS mode.
 * @return true if BoringSSL was built in a FIPS-compliant mode, false otherwise.
 */
bool isFIPS();

} // namespace Ssl
} // namespace Envoy
