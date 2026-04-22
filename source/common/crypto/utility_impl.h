#pragma once

#include "source/common/crypto/crypto_impl.h"
#include "source/common/crypto/utility.h"

#include "absl/types/span.h"
#include "openssl/bio.h"
#include "openssl/bytestring.h"
#include "openssl/hmac.h"
#include "openssl/sha.h"

namespace Envoy {
namespace Common {
namespace Crypto {

class UtilityImpl : public Envoy::Common::Crypto::Utility {
public:
  std::vector<uint8_t> getSha256Digest(const Buffer::Instance& buffer) override;
  std::vector<uint8_t> getSha256Hmac(absl::Span<const uint8_t> key,
                                     absl::string_view message) override;
  absl::Status verifySignature(absl::string_view hash_function, PKeyObject& key,
                               absl::Span<const uint8_t> signature,
                               absl::Span<const uint8_t> text) override;
  absl::StatusOr<std::vector<uint8_t>> sign(absl::string_view hash_function, PKeyObject& key,
                                            absl::Span<const uint8_t> text) override;
  PKeyObjectPtr importPublicKeyPEM(absl::string_view key) override;
  PKeyObjectPtr importPublicKeyDER(absl::Span<const uint8_t> key) override;
  PKeyObjectPtr importPrivateKeyPEM(absl::string_view key) override;
  PKeyObjectPtr importPrivateKeyDER(absl::Span<const uint8_t> key) override;

private:
  const EVP_MD* getHashFunction(absl::string_view name);
};

} // namespace Crypto
} // namespace Common
} // namespace Envoy
