#include "source/common/crypto/utility_impl.h"

#include "source/common/common/assert.h"
#include "source/common/crypto/crypto_impl.h"

#include "absl/container/fixed_array.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Common {
namespace Crypto {

struct EVP_PKEY_CTX_deleter {
  void operator()(EVP_PKEY_CTX* ctx) const { EVP_PKEY_CTX_free(ctx); }
};

using EVP_PKEY_CTX_ptr = std::unique_ptr<EVP_PKEY_CTX, EVP_PKEY_CTX_deleter>;

std::vector<uint8_t> UtilityImpl::getSha256Digest(const Buffer::Instance& buffer) {
  std::vector<uint8_t> digest(SHA256_DIGEST_LENGTH);
  bssl::ScopedEVP_MD_CTX ctx;
  auto rc = EVP_DigestInit(ctx.get(), EVP_sha256());
  RELEASE_ASSERT(rc == 1, "Failed to init digest context");
  for (const auto& slice : buffer.getRawSlices()) {
    rc = EVP_DigestUpdate(ctx.get(), slice.mem_, slice.len_);
    RELEASE_ASSERT(rc == 1, "Failed to update digest");
  }
  rc = EVP_DigestFinal(ctx.get(), digest.data(), nullptr);
  RELEASE_ASSERT(rc == 1, "Failed to finalize digest");
  return digest;
}

std::vector<uint8_t> UtilityImpl::getSha256Hmac(const std::vector<uint8_t>& key,
                                                absl::string_view message) {
  std::vector<uint8_t> hmac(SHA256_DIGEST_LENGTH);
  const auto ret =
      HMAC(EVP_sha256(), key.data(), key.size(), reinterpret_cast<const uint8_t*>(message.data()),
           message.size(), hmac.data(), nullptr);
  RELEASE_ASSERT(ret != nullptr, "Failed to create HMAC");
  return hmac;
}

const EncryptionDecryptionOutput UtilityImpl::decrypt(CryptoObject& key,
                                                      const std::vector<uint8_t>& cipher_text) {
  // Step 1: get private key
  auto pkey_wrapper = Common::Crypto::Access::getTyped<Common::Crypto::PrivateKeyObject>(key);
  EVP_PKEY* pkey = pkey_wrapper->getEVP_PKEY();
  if (pkey == nullptr) {
    return {false, "failed to get private key for decryption"};
  }

  // Step 2: initialize EVP_PKEY_CTX
  EVP_PKEY_CTX_ptr ctx(EVP_PKEY_CTX_new(pkey, nullptr));
  int ok = EVP_PKEY_decrypt_init(ctx.get());
  if (!ok) {
    return {false, "failed to initialize private key for decryption"};
  }

  // Step 3: decrypt cipher text
  size_t plaintext_size;
  ok =
      EVP_PKEY_decrypt(ctx.get(), nullptr, &plaintext_size, cipher_text.data(), cipher_text.size());
  if (!ok) {
    return {false, "failed to get plaintext size"};
  }

  std::vector<uint8_t> plaintext(plaintext_size);
  ok = EVP_PKEY_decrypt(ctx.get(), plaintext.data(), &plaintext_size, cipher_text.data(),
                        cipher_text.size());

  // Step 4: check result
  std::string p_text(plaintext.begin(), plaintext.begin() + plaintext_size);
  if (ok == 1) {
    return {true, p_text};
  }

  return {false, "decryption failed"};
}

const EncryptionDecryptionOutput UtilityImpl::encrypt(CryptoObject& key,
                                                      const std::vector<uint8_t>& plaintext) {
  // Step 1: get public key
  auto pkey_wrapper = Common::Crypto::Access::getTyped<Common::Crypto::PublicKeyObject>(key);
  EVP_PKEY* pkey = pkey_wrapper->getEVP_PKEY();
  if (pkey == nullptr) {
    return {false, "failed to get public key for encryption"};
  }

  // Step 2: initialize EVP_PKEY_CTX
  EVP_PKEY_CTX_ptr ctx(EVP_PKEY_CTX_new(pkey, nullptr));
  int ok = EVP_PKEY_encrypt_init(ctx.get());
  if (!ok) {
    return {false, "failed to initialize public key for encryption"};
  }

  // Step 3: encrypt plaintext
  size_t cipher_text_size;
  ok = EVP_PKEY_encrypt(ctx.get(), nullptr, &cipher_text_size, plaintext.data(), plaintext.size());
  if (!ok) {
    return {false, "failed to get cipher text size"};
  }

  std::vector<uint8_t> cipher_text(cipher_text_size);
  ok = EVP_PKEY_encrypt(ctx.get(), cipher_text.data(), &cipher_text_size, plaintext.data(),
                        plaintext.size());

  // Step 4: check result
  std::string p_text(cipher_text.begin(), cipher_text.begin() + cipher_text_size);
  if (ok == 1) {
    return {true, p_text};
  }

  return {false, "encryption failed"};
}

const VerificationOutput UtilityImpl::verifySignature(absl::string_view hash, CryptoObject& key,
                                                      const std::vector<uint8_t>& signature,
                                                      const std::vector<uint8_t>& text) {
  // Step 1: initialize EVP_MD_CTX
  bssl::ScopedEVP_MD_CTX ctx;

  // Step 2: initialize EVP_MD
  const EVP_MD* md = getHashFunction(hash);

  if (md == nullptr) {
    return {false, absl::StrCat(hash, " is not supported.")};
  }
  // Step 3: initialize EVP_DigestVerify
  auto pkey_wrapper = Common::Crypto::Access::getTyped<Common::Crypto::PublicKeyObject>(key);
  EVP_PKEY* pkey = pkey_wrapper->getEVP_PKEY();

  if (pkey == nullptr) {
    return {false, "Failed to initialize digest verify."};
  }

  int ok = EVP_DigestVerifyInit(ctx.get(), nullptr, md, nullptr, pkey);
  if (!ok) {
    return {false, "Failed to initialize digest verify."};
  }

  // Step 4: verify signature
  ok = EVP_DigestVerify(ctx.get(), signature.data(), signature.size(), text.data(), text.size());

  // Step 5: check result
  if (ok == 1) {
    return {true, ""};
  }

  return {false, absl::StrCat("Failed to verify digest. Error code: ", ok)};
}

CryptoObjectPtr UtilityImpl::importPublicKey(const std::vector<uint8_t>& key) {
  CBS cbs({key.data(), key.size()});

  return std::make_unique<PublicKeyObject>(EVP_parse_public_key(&cbs));
}

CryptoObjectPtr UtilityImpl::importPrivateKey(const std::vector<uint8_t>& key) {
  CBS cbs({key.data(), key.size()});

  return std::make_unique<PrivateKeyObject>(EVP_parse_private_key(&cbs));
}

const EVP_MD* UtilityImpl::getHashFunction(absl::string_view name) {
  const std::string hash = absl::AsciiStrToLower(name);

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

// Register the crypto utility singleton.
static Crypto::ScopedUtilitySingleton* utility_ =
    new Crypto::ScopedUtilitySingleton(std::make_unique<Crypto::UtilityImpl>());

} // namespace Crypto
} // namespace Common
} // namespace Envoy
