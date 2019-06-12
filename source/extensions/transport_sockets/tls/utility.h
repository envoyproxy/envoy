#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"

#include "common/common/utility.h"

#include "absl/strings/string_view.h"
#include "openssl/evp.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

struct VerificationOutput {
  /**
   * Verification result. If result_ is true, error_message_ is empty.
   */
  bool result_;

  /**
   * Error message when verification failed.
   * TODO(crazyxy): switch to absl::StatusOr when available
   */
  std::string error_message_;
};

typedef bssl::UniquePtr<EVP_PKEY> PublicKeyPtr;

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
 * @param type type of subject alternate name either GEN_DNS or GEN_URI
 * @return std::vector returns the list of subject alternate names.
 */
std::vector<std::string> getSubjectAltNames(X509& cert, int type);

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
 * Computes the SHA-256 digest of a buffer.
 * @param buffer the buffer.
 * @return a vector of bytes for the computed digest.
 */
std::vector<uint8_t> getSha256Digest(const Buffer::Instance& buffer);

/**
 * Computes the SHA-256 HMAC for a given key and message.
 * @param key the HMAC function key.
 * @param message message data for the HMAC function.
 * @return a vector of bytes for the computed HMAC.
 */
std::vector<uint8_t> getSha256Hmac(const std::vector<uint8_t>& key, absl::string_view message);

/**
 * Verify cryptographic signatures.
 * @param hash hash function(including SHA1, SHA224, SHA256, SHA384, SHA512)
 * @param key pointer to public key
 * @param signature signature
 * @param text clear text
 * @return If the result_ is true, the error_message_ is empty; otherwise,
 * the error_message_ stores the error message
 */
const VerificationOutput verifySignature(absl::string_view hash, EVP_PKEY* key,
                                         const std::vector<uint8_t>& signature,
                                         const std::vector<uint8_t>& text);

/**
 * Import public key.
 * @param key key string
 * @return pointer to public key
 */
PublicKeyPtr importPublicKey(const std::vector<uint8_t>& key);

const EVP_MD* getHashFunction(absl::string_view name);

} // namespace Utility
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
