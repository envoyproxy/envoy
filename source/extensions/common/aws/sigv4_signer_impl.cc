#include "source/extensions/common/aws/sigv4_signer_impl.h"

#include <openssl/ssl.h>

#include <cstddef>

#include "envoy/common/exception.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/fmt.h"
#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/headers.h"
#include "source/extensions/common/aws/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

std::string SigV4SignerImpl::createCredentialScope(absl::string_view short_date,
                                                   absl::string_view override_region) const {
  return fmt::format(SigV4SignatureConstants::SigV4CredentialScopeFormat, short_date,
                     override_region.empty() ? region_ : override_region, service_name_);
}

std::string SigV4SignerImpl::createStringToSign(absl::string_view canonical_request,
                                                absl::string_view long_date,
                                                absl::string_view credential_scope) const {
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  return fmt::format(
      SigV4SignatureConstants::SigV4StringToSignFormat, SigV4SignatureConstants::SigV4Algorithm,
      long_date, credential_scope,
      Hex::encode(crypto_util.getSha256Digest(Buffer::OwnedImpl(canonical_request))));
}

std::string SigV4SignerImpl::createSignature(const Credentials credentials,
                                             const absl::string_view short_date,
                                             const absl::string_view string_to_sign,
                                             const absl::string_view override_region) const {

  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  const auto secret_key = absl::StrCat(SigV4SignatureConstants::SigV4SignatureVersion,
                                       credentials.secretAccessKey().value());
  const auto date_key = crypto_util.getSha256Hmac(
      std::vector<uint8_t>(secret_key.begin(), secret_key.end()), short_date);
  const auto region_key =
      crypto_util.getSha256Hmac(date_key, override_region.empty() ? region_ : override_region);
  const auto service_key = crypto_util.getSha256Hmac(region_key, service_name_);
  const auto signing_key =
      crypto_util.getSha256Hmac(service_key, SigV4SignatureConstants::Aws4Request);
  return Hex::encode(crypto_util.getSha256Hmac(signing_key, string_to_sign));
}

std::string SigV4SignerImpl::createAuthorizationHeader(
    const Credentials credentials, const absl::string_view credential_scope,
    const std::map<std::string, std::string>& canonical_headers,
    absl::string_view signature) const {
  const auto signed_headers = Utility::joinCanonicalHeaderNames(canonical_headers);
  return fmt::format(SigV4SignatureConstants::SigV4AuthorizationHeaderFormat,
                     SigV4SignatureConstants::SigV4Algorithm,
                     createAuthorizationCredential(credentials, credential_scope), signed_headers,
                     signature);
}

absl::string_view SigV4SignerImpl::getAlgorithmString() const {
  return SigV4SignatureConstants::SigV4Algorithm;
}

std::string SigV4SignerImpl::createStringToSign(const X509Credentials x509_credentials,
                                                const absl::string_view canonical_request,
                                                const absl::string_view long_date,
                                                const absl::string_view credential_scope) const {
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  if (x509_credentials.publicKeySignatureAlgorithm() ==
      X509Credentials::PublicKeySignatureAlgorithm::RSA) {
    return fmt::format(
        SigV4SignatureConstants::SigV4StringToSignFormat, SigV4SignatureConstants::X509SigV4RSA,
        long_date, credential_scope,
        Hex::encode(crypto_util.getSha256Digest(Buffer::OwnedImpl(canonical_request))));
  } else {
    return fmt::format(
        SigV4SignatureConstants::SigV4StringToSignFormat, SigV4SignatureConstants::X509SigV4ECDSA,
        long_date, credential_scope,
        Hex::encode(crypto_util.getSha256Digest(Buffer::OwnedImpl(canonical_request))));
  }
}

std::string SigV4SignerImpl::createSignature(const X509Credentials x509_credentials,
                                             const absl::string_view string_to_sign) const {

  std::string key = x509_credentials.certificatePrivateKey().value();
  auto keysize = x509_credentials.certificatePrivateKey().value().size();
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(key.c_str(), keysize));

  bssl::UniquePtr<EVP_PKEY> pkey(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));

  auto pkey_size = EVP_PKEY_size(pkey.get());
  EVP_MD_CTX ctx;
  EVP_MD_CTX_init(&ctx);

  auto evp = EVP_get_digestbyname("sha256");
  EVP_SignInit(&ctx, evp);
  EVP_SignUpdate(&ctx, string_to_sign.data(), string_to_sign.size());

  std::vector<unsigned char> output(pkey_size);
  unsigned int sig_len;
  EVP_SignFinal(&ctx, output.data(), &sig_len, pkey.get());
  output.resize(sig_len);
  EVP_MD_CTX_cleanup(&ctx);
  return Hex::encode(output);
}

std::string SigV4SignerImpl::createAuthorizationHeader(
    const X509Credentials x509_credentials, const absl::string_view credential_scope,
    const std::map<std::string, std::string>& canonical_headers,
    const absl::string_view signature) const {

  const auto signed_headers = Utility::joinCanonicalHeaderNames(canonical_headers);

  if (x509_credentials.publicKeySignatureAlgorithm() ==
      X509Credentials::PublicKeySignatureAlgorithm::RSA) {
    return fmt::format(SigV4SignatureConstants::SigV4AuthorizationHeaderFormat,
                       SigV4SignatureConstants::X509SigV4RSA,
                       createAuthorizationCredential(x509_credentials, credential_scope),
                       signed_headers, signature);

  } else {
    return fmt::format(SigV4SignatureConstants::SigV4AuthorizationHeaderFormat,
                       SigV4SignatureConstants::X509SigV4ECDSA,
                       createAuthorizationCredential(x509_credentials, credential_scope),
                       signed_headers, signature);
  }
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
