#include "common/crypto/utility.h"

#include "common/common/assert.h"
#include "common/common/stack_array.h"

#include "openssl/evp.h"
#include "openssl/hmac.h"
#include "openssl/sha.h"

namespace Envoy {
namespace Common {
namespace Crypto {

std::vector<uint8_t> Utility::getSha256Digest(const Buffer::Instance& buffer) {
  std::vector<uint8_t> digest(SHA256_DIGEST_LENGTH);
  EVP_MD_CTX* ctx(EVP_MD_CTX_new());
  auto rc = EVP_DigestInit(ctx, EVP_sha256());
  RELEASE_ASSERT(rc == 1, "Failed to init digest context");
  const auto num_slices = buffer.getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, Buffer::RawSlice, num_slices);
  buffer.getRawSlices(slices.begin(), num_slices);
  for (const auto& slice : slices) {
    rc = EVP_DigestUpdate(ctx, slice.mem_, slice.len_);
    RELEASE_ASSERT(rc == 1, "Failed to update digest");
  }
  unsigned int len;
  rc = EVP_DigestFinal(ctx, digest.data(), &len);
  RELEASE_ASSERT(rc == 1, "Failed to finalize digest");
  EVP_MD_CTX_free(ctx);
  return digest;
}

std::vector<uint8_t> Utility::getSha256Hmac(const std::vector<uint8_t>& key,
                                            absl::string_view message) {
  std::vector<uint8_t> hmac(SHA256_DIGEST_LENGTH);
  unsigned int len;
  const auto ret =
      HMAC(EVP_sha256(), key.data(), key.size(), reinterpret_cast<const uint8_t*>(message.data()),
           message.size(), hmac.data(), &len);
  RELEASE_ASSERT(ret != nullptr, "Failed to create HMAC");
  return hmac;
}

} // namespace Crypto
} // namespace Common
} // namespace Envoy
