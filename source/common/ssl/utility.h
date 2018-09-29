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
const std::string getSerialNumberFromCertificate(X509& cert);

/**
 * Retrieves the subject alternate names of a certificate.
 * @param cert the certificate
 * @return std::vector returns the list of subject alternate names.
 */
const std::vector<std::string> getSubjectAltNames(X509& cert);

/**
 * Formats the subject alternate names of a certificate in a comma separated string
 * @param std::vector list of subject alternate names
 * @return std::string comma separated list of subject alternate names.
 */
const std::string formattedSubjectAltNames(const std::vector<std::string>& subject_alt_names);

} // namespace Utility
} // namespace Ssl
} // namespace Envoy
