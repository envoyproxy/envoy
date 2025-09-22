#pragma once

#include <cstdint>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/common/crypto/crypto.h"

#include "source/common/singleton/threadsafe_singleton.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

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
  virtual std::vector<uint8_t> getSha256Hmac(const std::vector<uint8_t>& key,
                                             absl::string_view message) PURE;

  /**
   * Verify cryptographic signatures.
   * @param hash hash function(including SHA1, SHA224, SHA256, SHA384, SHA512)
   * @param key CryptoObject containing EVP_PKEY public key (must be imported via importPublicKey())
   * @param signature signature bytes to verify
   * @param text clear text that was signed
   * @return absl::StatusOr<bool> containing true if verification succeeds, or error status on
   * failure
   * @note The key must be imported using importPublicKey() which supports both DER and PEM formats
   * @note Works with public keys imported from DER (PKCS#1) or PEM (PKCS#1/PKCS#8) formats
   */
  virtual absl::StatusOr<bool> verifySignature(absl::string_view hash, CryptoObject& key,
                                               const std::vector<uint8_t>& signature,
                                               const std::vector<uint8_t>& text) PURE;

  /**
   * Sign data with a private key.
   * @param hash hash function(including SHA1, SHA224, SHA256, SHA384, SHA512)
   * @param key CryptoObject containing EVP_PKEY private key (must be imported via
   * importPrivateKey())
   * @param text clear text to sign
   * @return absl::StatusOr<std::vector<uint8_t>> containing the signature on success, or error
   * status on failure
   * @note The key must be imported using importPrivateKey() which supports both DER and PEM formats
   * @note Works with private keys imported from DER (PKCS#8) or PEM (PKCS#1/PKCS#8) formats
   */
  virtual absl::StatusOr<std::vector<uint8_t>> sign(absl::string_view hash, CryptoObject& key,
                                                    const std::vector<uint8_t>& text) PURE;

  /**
   * Import public key from PEM format.
   * @param key Public key in PEM format
   * @return pointer to EVP_PKEY public key
   */
  virtual CryptoObjectPtr importPublicKeyPEM(const std::vector<uint8_t>& key) PURE;

  /**
   * Import public key from DER format.
   * @param key Public key in DER format
   * @return pointer to EVP_PKEY public key
   */
  virtual CryptoObjectPtr importPublicKeyDER(const std::vector<uint8_t>& key) PURE;

  /**
   * Import private key from PEM format.
   * @param key Private key in PEM format
   * @return pointer to EVP_PKEY private key
   */
  virtual CryptoObjectPtr importPrivateKeyPEM(const std::vector<uint8_t>& key) PURE;

  /**
   * Import private key from DER format.
   * @param key Private key in DER format
   * @return pointer to EVP_PKEY private key
   */
  virtual CryptoObjectPtr importPrivateKeyDER(const std::vector<uint8_t>& key) PURE;
};

using UtilitySingleton = InjectableSingleton<Utility>;
using ScopedUtilitySingleton = ScopedInjectableLoader<Utility>;

} // namespace Crypto
} // namespace Common
} // namespace Envoy
