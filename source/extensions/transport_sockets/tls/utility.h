#pragma once

#include <string>
#include <vector>

#include "common/common/utility.h"

#include "absl/types/optional.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace Utility {

/**
 * Retrieves the serial number of a certificate.
 * @param cert the certificate
 * @return std::string the serial number field of the certificate. Returns "" if
 *         there is no serial number.
 */
std::string getSerialNumberFromCertificate(X509& cert);

/**
 * Retrieves the subject alternate names of a certificate.
 * @param cert the certificate
 * @param type type of subject alternate name
 * @return std::vector returns the list of subject alternate names.
 */
std::vector<std::string> getSubjectAltNames(X509& cert, int type);

/**
 * Converts the Subject Alternate Name to string.
 * @param general_name the subject alternate name
 * @return std::string returns the string representation of subject alt names.
 */
std::string generalNameAsString(const GENERAL_NAME* general_name);

/**
 * Retrieves the issuer from certificate.
 * @param cert the certificate
 * @return std::string the issuer field for the certificate.
 */
std::string getIssuerFromCertificate(X509& cert);

/**
 * Retrieves the subject from certificate.
 * @param cert the certificate
 * @return std::string the subject field for the certificate.
 */
std::string getSubjectFromCertificate(X509& cert);

/**
 * Retrieves the value of a specific X509 extension from the cert, if present.
 * @param cert the certificate.
 * @param extension_name the name of the extension to extract.
 * @return absl::optional<std::string> the value of the extension field, if present.
 */
absl::optional<std::string> getX509ExtensionValue(const X509& cert,
                                                  absl::string_view extension_name);

/**
 * Returns the days until this certificate is valid.
 * @param cert the certificate
 * @param time_source the time source to use for current time calculation.
 * @return the number of days till this certificate is valid.
 */
int32_t getDaysUntilExpiration(const X509* cert, TimeSource& time_source);

/**
 * Returns the time from when this certificate is valid.
 * @param cert the certificate.
 * @return time from when this certificate is valid.
 */
SystemTime getValidFrom(const X509& cert);

/**
 * Returns the time when this certificate expires.
 * @param cert the certificate.
 * @return time after which the certificate expires.
 */
SystemTime getExpirationTime(const X509& cert);

/**
 * Returns the last crypto error from ERR_get_error(), or `absl::nullopt`
 * if the error stack is empty.
 * @return std::string error message
 */
absl::optional<std::string> getLastCryptoError();

} // namespace Utility
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
