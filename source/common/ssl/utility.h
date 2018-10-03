#pragma once

#include <string>
#include <vector>

#include "openssl/ssl.h"

namespace Envoy {
namespace Ssl {
namespace Utility {

/**
 * Retrieves the serial number of a certificate.
 * @param cert the certificate
 * @return std::string the serial number field of the certificate. Returns "" if
 *         there is no serial number.
 */
std::string getSerialNumberFromCertificate(X509& cert);

/**
 * Retrieves the subject alternate names of a certificate of type DNS.
 * @param cert the certificate
 * @return std::vector returns the list of subject alternate names.
 */
std::vector<std::string> getDnsSubjectAltNames(X509& cert);

/**
 * Retrieves the subject alternate names of a certificate of type URI.
 * @param cert the certificate
 * @return std::vector returns the list of subject alternate names.
 */
std::vector<std::string> getUriSubjectAltNames(X509& cert);

} // namespace Utility
} // namespace Ssl
} // namespace Envoy
