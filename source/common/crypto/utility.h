#pragma once

#include <cstdint>
#include <vector>

#include "envoy/buffer/buffer.h"

#include "source/common/crypto/crypto_impl.h"
#include "source/common/singleton/threadsafe_singleton.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace Envoy {
namespace Common {
namespace Crypto {

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
  virtual std::vector<uint8_t> getSha256Hmac(absl::Span<const uint8_t> key,
                                             absl::string_view message) PURE;

  /**
   * Verify cryptographic signatures.
   * @param hash_function hash function name (including SHA1, SHA224, SHA256, SHA384, SHA512)
   * @param key CryptoObject containing EVP_PKEY public key (must be imported via importPublicKey())
   * @param signature signature bytes to verify
   * @param text clear text that was signed
   * @return absl::Status containing Ok if verification succeeds, or error status on failure
   * @note The key must be imported using importPublicKey() which supports both DER and PEM formats
   * @note Works with public keys imported from DER (PKCS#1) or PEM (PKCS#1/PKCS#8) formats
   */
  virtual absl::Status verifySignature(absl::string_view hash_function, PKeyObject& key,
                                       absl::Span<const uint8_t> signature,
                                       absl::Span<const uint8_t> text) PURE;

  /**
   * Sign data with a private key.
   * @param hash_function hash function name (including SHA1, SHA224, SHA256, SHA384, SHA512)
   * @param key CryptoObject containing EVP_PKEY private key (must be imported via
   * importPrivateKey())
   * @param text clear text to sign
   * @return absl::StatusOr<std::vector<uint8_t>> containing the signature on success, or error
   * status on failure
   * @note The key must be imported using importPrivateKey() which supports both DER and PEM formats
   * @note Works with private keys imported from DER (PKCS#8) or PEM (PKCS#1/PKCS#8) formats
   */
  virtual absl::StatusOr<std::vector<uint8_t>>
  sign(absl::string_view hash_function, PKeyObject& key, absl::Span<const uint8_t> text) PURE;

  /**
   * Import public key from PEM format.
   * @param key Public key in PEM format
   * @return pointer to EVP_PKEY public key
   */
  virtual PKeyObjectPtr importPublicKeyPEM(absl::string_view key) PURE;

  /**
   * Import public key from DER format.
   * @param key Public key in DER format
   * @return pointer to EVP_PKEY public key
   */
  virtual PKeyObjectPtr importPublicKeyDER(absl::Span<const uint8_t> key) PURE;

  /**
   * Import private key from PEM format.
   * @param key Private key in PEM format
   * @return pointer to EVP_PKEY private key
   */
  virtual PKeyObjectPtr importPrivateKeyPEM(absl::string_view key) PURE;

  /**
   * Import private key from DER format.
   * @param key Private key in DER format
   * @return pointer to EVP_PKEY private key
   */
  virtual PKeyObjectPtr importPrivateKeyDER(absl::Span<const uint8_t> key) PURE;
};

using UtilitySingleton = InjectableSingleton<Utility>;
using ScopedUtilitySingleton = ScopedInjectableLoader<Utility>;

} // namespace Crypto
} // namespace Common
} // namespace Envoy
