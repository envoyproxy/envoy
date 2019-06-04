#include "common/crypto/utility.h"

#include "common/common/assert.h"
#include "common/common/stack_array.h"

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "openssl/bytestring.h"
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

VerificationOutput Utility::verifySignature(const absl::string_view& hash, void* ptr,
                                            const std::vector<uint8_t>& signature,
                                            const std::vector<uint8_t>& clearText) {
  // Step 1: get public key
  auto pubkey = reinterpret_cast<EVP_PKEY*>(ptr);

  // Step 2: initialize EVP_MD_CTX
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();

  // Step 3: initialize EVP_MD
  const EVP_MD* md = Utility::getHashFunction(hash);

  if (md == nullptr) {
    EVP_MD_CTX_free(ctx);
    return {false, absl::StrCat(hash, " is not supported.")};
  }

  // Step 4: initialize EVP_DigestVerify
  int ok = EVP_DigestVerifyInit(ctx, nullptr, md, nullptr, pubkey);
  if (!ok) {
    EVP_MD_CTX_free(ctx);
    return {false, "Failed to initialize digest verify."};
  }

  // Step 5: verify signature
  ok =
      EVP_DigestVerify(ctx, signature.data(), signature.size(), clearText.data(), clearText.size());

  // Step 6: check result
  if (ok == 1) {
    EVP_MD_CTX_free(ctx);
    return {true, ""};
  }

  EVP_MD_CTX_free(ctx);
  std::string err_msg;
  absl::StrAppend(&err_msg, "Failed to verify digest. Error code: ", ok);
  return {false, err_msg};
}

void* Utility::importPublicKey(const std::vector<uint8_t>& key) {
  CBS cbs({key.data(), key.size()});
  return EVP_parse_public_key(&cbs);
}

void Utility::releasePublicKey(void* ptr) { EVP_PKEY_free(reinterpret_cast<EVP_PKEY*>(ptr)); }

const EVP_MD* Utility::getHashFunction(const absl::string_view& name) {
  std::string hash = absl::AsciiStrToLower(name);

  // Hash algorithms set refers
  // https://github.com/google/boringssl/blob/master/include/openssl/digest.h
  if (hash == "sha1") {
    return EVP_sha1();
  } else if (hash == "sha224") {
    return EVP_sha224();
  } else if (hash == "sha256") {
    return EVP_sha256();
  } else if (hash == "sha384") {
    return EVP_sha384();
  } else if (hash == "sha512") {
    return EVP_sha512();
  } else {
    return nullptr;
  }
}

} // namespace Crypto
} // namespace Common
} // namespace Envoy
