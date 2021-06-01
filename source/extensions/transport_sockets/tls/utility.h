#pragma once

#include <string>
#include <vector>

#include "envoy/ssl/context.h"

#include "common/common/utility.h"

#include "absl/types/optional.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace Utility {

static constexpr absl::string_view SSL_ERROR_NONE_MESSAGE = "NONE";
static constexpr absl::string_view SSL_ERROR_SSL_MESSAGE = "SSL";
static constexpr absl::string_view SSL_ERROR_WANT_READ_MESSAGE = "WANT_READ";
static constexpr absl::string_view SSL_ERROR_WANT_WRITE_MESSAGE = "WANT_WRITE";
static constexpr absl::string_view SSL_ERROR_WANT_X509_LOOPUP_MESSAGE = "WANT_X509_LOOKUP";
static constexpr absl::string_view SSL_ERROR_SYSCALL_MESSAGE = "SYSCALL";
static constexpr absl::string_view SSL_ERROR_ZERO_RETURN_MESSAGE = "ZERO_RETURN";
static constexpr absl::string_view SSL_ERROR_WANT_CONNECT_MESSAGE = "WANT_CONNECT";
static constexpr absl::string_view SSL_ERROR_WANT_ACCEPT_MESSAGE = "WANT_ACCEPT";
static constexpr absl::string_view SSL_ERROR_WANT_CHANNEL_ID_LOOKUP_MESSAGE =
    "WANT_CHANNEL_ID_LOOKUP";
static constexpr absl::string_view SSL_ERROR_PENDING_SESSION_MESSAGE = "PENDING_SESSION";
static constexpr absl::string_view SSL_ERROR_PENDING_CERTIFICATE_MESSAGE = "PENDING_CERTIFICATE";
static constexpr absl::string_view SSL_ERROR_WANT_PRIVATE_KEY_OPERATION_MESSAGE =
    "WANT_PRIVATE_KEY_OPERATION";
static constexpr absl::string_view SSL_ERROR_PENDING_TICKET_MESSAGE = "PENDING_TICKET";
static constexpr absl::string_view SSL_ERROR_EARLY_DATA_REJECTED_MESSAGE = "EARLY_DATA_REJECTED";
static constexpr absl::string_view SSL_ERROR_WANT_CERTIFICATE_VERIFY_MESSAGE =
    "WANT_CERTIFICATE_VERIFY";
static constexpr absl::string_view SSL_ERROR_HANDOFF_MESSAGE = "HANDOFF";
static constexpr absl::string_view SSL_ERROR_HANDBACK_MESSAGE = "HANDBACK";
static constexpr absl::string_view SSL_ERROR_UNKNOWN_ERROR_MESSAGE = "UNKNOWN_ERROR";

Envoy::Ssl::CertificateDetailsPtr certificateDetails(X509* cert, const std::string& path,
                                                     TimeSource& time_source);

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
 * @param extension_name the name of the extension to extract in dotted number format
 * @return absl::string_view the DER-encoded value of the extension field or empty if not present.
 */
absl::string_view getCertificateExtensionValue(X509& cert, absl::string_view extension_name);

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

/**
 * Returns error string corresponding error code derived from OpenSSL.
 * @param err error code
 * @return string message corresponding error code.
 */
absl::string_view getErrorDescription(int err);

} // namespace Utility
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
