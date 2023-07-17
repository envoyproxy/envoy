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

struct EncryptionDecryptionOutput {
  /**
   * Result of the encryption/decryption operation. If this is false, then we populate the data
   * field with the error message returned by the function or otherwise we populate the data field
   * with the encrypted/decrypted text.
   */
  bool result_;

  /**
   * It contains the encrypted/decrypted text if the operation succeeds or otherwise the error
   * message.
   */
  std::string data_;
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
   * @param message message data for the HMAC function.
   * @return a vector of bytes for the computed HMAC.
   */
  virtual std::vector<uint8_t> getSha256Hmac(const std::vector<uint8_t>& key,
                                             absl::string_view message) PURE;

  /**
   * Verify cryptographic signatures.
   * @param hash hash function(including SHA1, SHA224, SHA256, SHA384, SHA512)
   * @param key pointer to EVP_PKEY public key
   * @param signature signature
   * @param text clear text
   * @return If the result_ is true, the error_message_ is empty; otherwise,
   * the error_message_ stores the error message
   */
  virtual const VerificationOutput verifySignature(absl::string_view hash, CryptoObject& key,
                                                   const std::vector<uint8_t>& signature,
                                                   const std::vector<uint8_t>& text) PURE;

  /**
   * Decrypt cipher text using the provided private key.
   * @param key pointer to EVP_PKEY private key
   * @param cipher_text cipher text
   * @return Returns the result of the decrypt operation with the decrypted message or error message
   * if unsuccessful.
   */
  virtual const EncryptionDecryptionOutput decrypt(CryptoObject& key,
                                                   const std::vector<uint8_t>& cipher_text) PURE;

  /**
   * Encrypt plaintext using the provided public key.
   * @param key pointer to EVP_PKEY public key
   * @param plaintext plaintext message
   * @return Returns the result of the encrypt operation with the encrypted message or error message
   * if unsuccessful.
   */
  virtual const EncryptionDecryptionOutput encrypt(CryptoObject& key,
                                                   const std::vector<uint8_t>& cipher_text) PURE;

  /**
   * Import public key.
   * @param key key string
   * @return pointer to EVP_PKEY public key
   */
  virtual CryptoObjectPtr importPublicKey(const std::vector<uint8_t>& key) PURE;

  /**
   * Import private key.
   * @param key key string
   * @return pointer to EVP_PKEY private key
   */
  virtual CryptoObjectPtr importPrivateKey(const std::vector<uint8_t>& key) PURE;
};

using UtilitySingleton = InjectableSingleton<Utility>;
using ScopedUtilitySingleton = ScopedInjectableLoader<Utility>;

} // namespace Crypto
} // namespace Common
} // namespace Envoy
