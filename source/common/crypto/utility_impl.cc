#include "source/common/crypto/utility_impl.h"

#include "source/common/common/assert.h"
#include "source/common/crypto/crypto_impl.h"

#include "absl/container/fixed_array.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Common {
namespace Crypto {

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

const VerificationOutput UtilityImpl::verifySignature(absl::string_view hash, CryptoObject& key,
                                                      const std::vector<uint8_t>& signature,
                                                      const std::vector<uint8_t>& text) {
  // Verify cryptographic signature using a public key
  // The key must be imported via importPublicKey() which supports both DER and PEM formats
  // Step 1: initialize EVP_MD_CTX
  bssl::ScopedEVP_MD_CTX ctx;

  // Step 2: initialize EVP_MD
  const EVP_MD* md = getHashFunction(hash);

  if (md == nullptr) {
    return {false, absl::StrCat(hash, " is not supported.")};
  }
  // Step 3: initialize EVP_DigestVerify
  auto pkey_wrapper = Common::Crypto::Access::getTyped<Common::Crypto::PublicKeyObject>(key);
  if (pkey_wrapper == nullptr) {
    return {false, "Failed to initialize digest verify."};
  }
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

const SignOutput UtilityImpl::sign(absl::string_view hash, CryptoObject& key,
                                   const std::vector<uint8_t>& text) {
  // Sign data using a private key
  // The key must be imported via importPrivateKey() which supports both DER and PEM formats
  // Step 1: initialize EVP_MD_CTX
  bssl::ScopedEVP_MD_CTX ctx;

  // Step 2: initialize EVP_MD
  const EVP_MD* md = getHashFunction(hash);

  if (md == nullptr) {
    return {false, {}, absl::StrCat(hash, " is not supported.")};
  }

  // Step 3: initialize EVP_DigestSign
  auto pkey_wrapper = Common::Crypto::Access::getTyped<Common::Crypto::PrivateKeyObject>(key);
  if (pkey_wrapper == nullptr) {
    return {false, {}, "Failed to initialize digest sign."};
  }
  EVP_PKEY* pkey = pkey_wrapper->getEVP_PKEY();

  if (pkey == nullptr) {
    return {false, {}, "Failed to initialize digest sign."};
  }

  int ok = EVP_DigestSignInit(ctx.get(), nullptr, md, nullptr, pkey);
  if (!ok) {
    return {false, {}, "Failed to initialize digest sign."};
  }

  // Step 4: get signature length
  size_t sig_len = 0;
  ok = EVP_DigestSign(ctx.get(), nullptr, &sig_len, text.data(), text.size());
  if (!ok) {
    return {false, {}, "Failed to get signature length."};
  }

  // Step 5: create signature
  std::vector<uint8_t> signature(sig_len);
  ok = EVP_DigestSign(ctx.get(), signature.data(), &sig_len, text.data(), text.size());
  if (!ok) {
    return {false, {}, "Failed to create signature."};
  }

  // Step 6: resize signature to actual length and return
  signature.resize(sig_len);
  return {true, signature, ""};
}

bool UtilityImpl::isPEMFormat(const std::vector<uint8_t>& key) {
  // PEM format detection: looks for "-----BEGIN" markers and newlines
  // DER format: binary data without PEM markers (typically hex-encoded)
  if (key.size() <= 10) {
    return false;
  }

  std::string key_str(key.begin(), key.end());
  return key_str.find("-----BEGIN") != std::string::npos && key_str.find('\n') != std::string::npos;
}

CryptoObjectPtr UtilityImpl::importPublicKeyPEM(const std::vector<uint8_t>& key) {
  // PEM format: Use PEM parsing which automatically handles both PKCS#1 and PKCS#8 formats
  // This resolves the format inconsistency issue when using PEM keys
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(key.data(), key.size()));
  if (!bio) {
    return std::make_unique<PublicKeyObject>(nullptr);
  }
  return std::make_unique<PublicKeyObject>(
      PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr));
}

CryptoObjectPtr UtilityImpl::importPublicKeyDER(const std::vector<uint8_t>& key) {
  // DER format: Use DER parsing (expects SubjectPublicKeyInfo format containing PKCS#1 key)
  // This maintains backward compatibility with existing hex-encoded DER keys
  CBS cbs({key.data(), key.size()});
  return std::make_unique<PublicKeyObject>(EVP_parse_public_key(&cbs));
}

CryptoObjectPtr UtilityImpl::importPublicKey(const std::vector<uint8_t>& key) {
  // Auto-detect format: PEM or DER
  if (isPEMFormat(key)) {
    return importPublicKeyPEM(key);
  } else {
    return importPublicKeyDER(key);
  }
}

CryptoObjectPtr UtilityImpl::importPrivateKeyPEM(const std::vector<uint8_t>& key) {
  // PEM format: Use PEM parsing which automatically handles both PKCS#1 and PKCS#8 formats
  // This resolves the format inconsistency issue when using PEM keys
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(key.data(), key.size()));
  if (!bio) {
    return std::make_unique<PrivateKeyObject>(nullptr);
  }
  return std::make_unique<PrivateKeyObject>(
      PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
}

CryptoObjectPtr UtilityImpl::importPrivateKeyDER(const std::vector<uint8_t>& key) {
  // DER format: Use DER parsing (expects PKCS#8 PrivateKeyInfo format)
  // This maintains backward compatibility with existing hex-encoded DER keys
  CBS cbs({key.data(), key.size()});
  return std::make_unique<PrivateKeyObject>(EVP_parse_private_key(&cbs));
}

CryptoObjectPtr UtilityImpl::importPrivateKey(const std::vector<uint8_t>& key) {
  // Auto-detect format: PEM or DER
  if (isPEMFormat(key)) {
    return importPrivateKeyPEM(key);
  } else {
    return importPrivateKeyDER(key);
  }
}

const EVP_MD* UtilityImpl::getHashFunction(absl::string_view name) {
  const std::string hash = absl::AsciiStrToLower(name);

  // Hash algorithms set refers
  // https://github.com/google/boringssl/blob/main/include/openssl/digest.h
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
