#pragma once

#include <cstdint>
#include <vector>

#include "envoy/buffer/buffer.h"

#include "absl/strings/string_view.h"
#include "openssl/evp.h"

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
   */
  std::string error_message_;
};

class Utility {
public:
  /**
   * Computes the SHA-256 digest of a buffer.
   * @param buffer the buffer.
   * @return a vector of bytes for the computed digest.
   */
  static std::vector<uint8_t> getSha256Digest(const Buffer::Instance& buffer);

  /**
   * Computes the SHA-256 HMAC for a given key and message.
   * @param key the HMAC function key.
   * @param message message data for the HMAC function.
   * @return a vector of bytes for the computed HMAC.
   */
  static std::vector<uint8_t> getSha256Hmac(const std::vector<uint8_t>& key,
                                            absl::string_view message);

  /**
   * Verify cryptographic signatures.
   * @param hash hash function(including SHA1, SHA224, SHA256, SHA384, SHA512)
   * @param key pointer to public key
   * @param signature signature
   * @param clearText clear text
   * @return If the result_ is true, the error_message_ is empty; otherwise,
   * the error_message_ stores the error message
   */
  static VerificationOutput verifySignature(const absl::string_view& hash, void* key,
                                            const std::vector<uint8_t>& signature,
                                            const std::vector<uint8_t>& clearText);

  /**
   * Import public key.
   * @param key key string
   * @return pointer to public key
   */
  static void* importPublicKey(const std::vector<uint8_t>& key);

  /**
   * Release public key.
   * @param key pointer to public key
   */
  static void releasePublicKey(void* key);

private:
  static const EVP_MD* getHashFunction(const absl::string_view& name);
};

} // namespace Crypto
} // namespace Common
} // namespace Envoy
