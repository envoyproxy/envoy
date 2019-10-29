#pragma once

#include "common/crypto/utility.h"

#include "openssl/bytestring.h"
#include "openssl/hmac.h"
#include "openssl/sha.h"

namespace Envoy {
namespace Common {
namespace Crypto {

class UtilityImpl : public Envoy::Common::Crypto::Utility {
public:
  // Crypto::Utility
  std::vector<uint8_t> getSha256Digest(const Buffer::Instance& buffer);
  std::vector<uint8_t> getSha256Hmac(const std::vector<uint8_t>& key, absl::string_view message);
  const VerificationOutput verifySignature(absl::string_view hash, CryptoObject& key,
                                           const std::vector<uint8_t>& signature,
                                           const std::vector<uint8_t>& text);
  CryptoObjectPtr importPublicKey(const std::vector<uint8_t>& key);

private:
  const EVP_MD* getHashFunction(absl::string_view name);
};

} // namespace Crypto
} // namespace Common
} // namespace Envoy
