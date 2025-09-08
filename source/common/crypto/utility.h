#pragma once

#include <cstdint>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/common/crypto/crypto.h"

#include "source/common/singleton/threadsafe_singleton.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Common {
namespace Crypto {

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

struct SignOutput {
  /**
   * Signing result. If result_ is true, error_message_ is empty.
   */
  bool result_;

  /**
   * Generated signature when signing succeeded.
   */
  std::vector<uint8_t> signature_;

  /**
   * Error message when signing failed.
   * TODO(crazyxy): switch to absl::StatusOr when available
   */
  std::string error_message_;
};

class Utility {
public:
  virtual ~Utility() = default;

  /**
   * Computes the SHA-256 digest of a buffer.
   * @param buffer the buffer.
   * @return a vector of bytes for the computed digest.
   */
  virtual std::vector<uint8_t> getSha256Digest(const Buffer::Instance& buffer) PURE;

  /**
   * Computes the SHA-256 HMAC for a given key and message.
   * @param key the HMAC function key.
   * @param message string_view message data for the HMAC function.
   * @return a vector of bytes for the computed HMAC.
   */
  virtual std::vector<uint8_t> getSha256Hmac(const std::vector<uint8_t>& key,
                                             absl::string_view message) PURE;

  /**
   * Verify cryptographic signatures.
   * @param hash hash function(including SHA1, SHA224, SHA256, SHA384, SHA512)
   * @param key CryptoObject containing EVP_PKEY public key (must be imported via importPublicKey())
   * @param signature signature bytes to verify
   * @param text clear text that was signed
   * @return If the result_ is true, the error_message_ is empty; otherwise,
   * the error_message_ stores the error message
   * @note The key must be imported using importPublicKey() which supports both DER and PEM formats
   * @note Works with public keys imported from DER (PKCS#1) or PEM (PKCS#1/PKCS#8) formats
   */
  virtual const VerificationOutput verifySignature(absl::string_view hash, CryptoObject& key,
                                                   const std::vector<uint8_t>& signature,
                                                   const std::vector<uint8_t>& text) PURE;

  /**
   * Sign data with a private key.
   * @param hash hash function(including SHA1, SHA224, SHA256, SHA384, SHA512)
   * @param key CryptoObject containing EVP_PKEY private key (must be imported via
   * importPrivateKey())
   * @param text clear text to sign
   * @return If the result_ is true, the signature_ contains the generated signature and
   * error_message_ is empty; otherwise, the error_message_ stores the error message
   * @note The key must be imported using importPrivateKey() which supports both DER and PEM formats
   * @note Works with private keys imported from DER (PKCS#8) or PEM (PKCS#1/PKCS#8) formats
   */
  virtual const SignOutput sign(absl::string_view hash, CryptoObject& key,
                                const std::vector<uint8_t>& text) PURE;

  /**
   * Import public key.
   * @param key Public key in DER (hex-encoded) or PEM format
   * @return pointer to EVP_PKEY public key
   * @note Supports both DER (hex-encoded) and PEM formats with auto-detection
   * @note DER format: SubjectPublicKeyInfo format (SEQUENCE { SEQUENCE { OID, NULL }, BIT STRING })
   * containing PKCS#1 key
   * @note PEM format: Automatically handles both PKCS#1 and PKCS#8 formats
   */
  virtual CryptoObjectPtr importPublicKey(const std::vector<uint8_t>& key) PURE;

  /**
   * Import private key.
   * @param key Private key in DER (hex-encoded) or PEM format
   * @return pointer to EVP_PKEY private key
   * @note Supports both DER (hex-encoded) and PEM formats with auto-detection
   * @note DER format: PKCS#8 PrivateKeyInfo format (SEQUENCE { INTEGER, SEQUENCE { OID, NULL },
   * OCTET STRING })
   * @note PEM format: Automatically handles both PKCS#1 and PKCS#8 formats
   */
  virtual CryptoObjectPtr importPrivateKey(const std::vector<uint8_t>& key) PURE;
};

using UtilitySingleton = InjectableSingleton<Utility>;
using ScopedUtilitySingleton = ScopedInjectableLoader<Utility>;

} // namespace Crypto
} // namespace Common
} // namespace Envoy
