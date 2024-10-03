#pragma once

#include <string>
#include <vector>

#include "envoy/ssl/context.h"

#include "source/common/common/utility.h"

#include "absl/types/optional.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace Utility {

Envoy::Ssl::CertificateDetailsPtr certificateDetails(X509* cert, const std::string& path,
                                                     TimeSource& time_source);

/**
 * Determines whether the given name matches 'pattern' which may optionally begin with a wildcard
 * or contain a wildcard inside the pattern's first label.
 * See: https://www.rfc-editor.org/rfc/rfc6125#section-6.4.3.
 * @param dns_name the DNS name to match
 * @param pattern the pattern to match against (*.example.com) or (test*.example.com)
 * @return true if the san matches pattern
 */
bool dnsNameMatch(absl::string_view dns_name, absl::string_view pattern);

/**
 * Determines whether the given DNS label matches 'pattern' which may contain a wildcard. e.g.,
 * patterns "baz*" and "*baz" and "b*z" would match DNS labels "baz1" and "foobaz" and "buzz",
 * respectively.
 * @param dns_label the DNS name label to match in lower case
 * @param pattern the pattern to match against in lower case
 * @return true if the dns_label matches pattern
 */
bool labelWildcardMatch(absl::string_view dns_label, absl::string_view pattern);

/**
 * Retrieves the serial number of a certificate.
 * @param cert the certificate
 * @return std::string the serial number field of the certificate. Returns "" if
 *         there is no serial number.
 */
std::string getSerialNumberFromCertificate(X509& cert);

/**
 * Maps a stack of x509 certificates to a vector of strings extracted from the certificates.
 * @param stack the stack of certificates
 * @param field_extractor the function to extract the field from each certificate.
 * @return std::vector<std::string> returns the list of fields extracted from the certificates.
 */
std::vector<std::string> mapX509Stack(stack_st_X509& stack,
                                      std::function<std::string(X509&)> field_extractor);

/**
 * Retrieves the subject alternate names of a certificate.
 * @param cert the certificate
 * @param type type of subject alternate name
 * @param skip_unsupported If true and a name is for an unsupported (on this host) IP version,
 *   omit that name from the return value. If false, an exception will be thrown in this situation.
 * @return std::vector returns the list of subject alternate names.
 */
std::vector<std::string> getSubjectAltNames(X509& cert, int type, bool skip_unsupported = false);

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
 * Retrieves the extension OIDs from certificate.
 * @param cert the certificate
 * @return std::vector returns the string list of ASN.1 object identifiers.
 */
std::vector<std::string> getCertificateExtensionOids(X509& cert);

/**
 * Retrieves the value of a specific X509 extension from the cert, if present.
 * @param cert the certificate.
 * @param extension_name the name of the extension to extract in dotted number format
 * @return absl::string_view the DER-encoded value of the extension field or empty if not present.
 */
absl::string_view getCertificateExtensionValue(X509& cert, absl::string_view extension_name);

/**
 * Returns the days until this certificate is valid.
 * @param cert the certificate
 * @param time_source the time source to use for current time calculation.
 * @return the number of days till this certificate is valid, the value is set when not expired.
 */
absl::optional<uint32_t> getDaysUntilExpiration(const X509* cert, TimeSource& time_source);

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

/**
 * Returns error string corresponding error code derived from OpenSSL.
 * @param err error code
 * @return string message corresponding error code.
 */
absl::string_view getErrorDescription(int err);

/**
 * Extracts the X509 certificate validation error information.
 *
 * @param ctx the store context
 * @return the error details
 */
std::string getX509VerificationErrorInfo(X509_STORE_CTX* ctx);

} // namespace Utility
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
