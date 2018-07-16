#pragma once

#include <string>

#include "openssl/ssl.h"

namespace Envoy {
namespace Ssl {
namespace Utility {

/**
 * Retrieve the serial number of a certificate.
 * @param ssl the certificate
 * @return std::string the serial number field of the certificate. Returns "" if
 *         there is no serial number.
 */
std::string getSerialNumberFromCertificate(X509& cert);

} // namespace Utility
} // namespace Ssl
} // namespace Envoy
