#include "source/extensions/common/aws/signers/iam_roles_anywhere_sigv4_signer_impl.h"

#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"
#include "source/extensions/common/aws/utility.h"

#include "openssl/base.h"
#include "openssl/bio.h"
#include "openssl/pem.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

std::string
IAMRolesAnywhereSigV4Signer::createCredentialScope(absl::string_view short_date,
                                                   absl::string_view override_region) const {
  return fmt::format(IAMRolesAnywhereSigV4SignatureConstants::SigV4CredentialScopeFormat,
                     short_date, override_region.empty() ? region_ : override_region,
                     service_name_);
}

std::string IAMRolesAnywhereSigV4Signer::createStringToSign(
    const X509Credentials& x509_credentials, const absl::string_view canonical_request,
    const absl::string_view long_date, const absl::string_view credential_scope) const {
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  return fmt::format(
      IAMRolesAnywhereSigV4SignatureConstants::SigV4StringToSignFormat,
      getAlgorithmName(x509_credentials), long_date, credential_scope,
      Hex::encode(crypto_util.getSha256Digest(Buffer::OwnedImpl(canonical_request))));
}

absl::StatusOr<std::string>
IAMRolesAnywhereSigV4Signer::createSignature(const X509Credentials& x509_credentials,
                                             const absl::string_view string_to_sign) const {

  unsigned int sig_len;

  std::string key = x509_credentials.certificatePrivateKey().value();
  auto keysize = x509_credentials.certificatePrivateKey().value().size();
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(key.c_str(), keysize));

  bssl::UniquePtr<EVP_PKEY> pkey(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
  if (pkey == nullptr) {
    return absl::InvalidArgumentError("createSignature: Failed to read private key");
  }

  auto pkey_size = EVP_PKEY_size(pkey.get());
  auto ctx = EVP_MD_CTX_new();
  RELEASE_ASSERT(ctx != nullptr, "createSignature: EVP_MD_CTX_new failure");
  EVP_MD_CTX_init(ctx);

  auto evp = EVP_get_digestbyname("sha256");
  RELEASE_ASSERT(evp != nullptr, "createSignature: EVP_get_digestbyname failure");
  RELEASE_ASSERT(EVP_SignInit_ex(ctx, evp, nullptr) == 1,
                 "createSignature: EVP_SignInit_ex failure");

  EVP_SignUpdate(ctx, string_to_sign.data(), string_to_sign.size());

  std::vector<unsigned char> output(pkey_size);
  RELEASE_ASSERT(EVP_SignFinal(ctx, output.data(), &sig_len, pkey.get()) == 1,
                 "createSignature: EVP_SignFinal failure");

  output.resize(sig_len);
  EVP_MD_CTX_free(ctx);
  return Hex::encode(output);
}

std::string IAMRolesAnywhereSigV4Signer::createAuthorizationHeader(
    const X509Credentials& x509_credentials, const absl::string_view credential_scope,
    const std::map<std::string, std::string>& canonical_headers,
    const absl::string_view signature) const {

  const auto signed_headers = Utility::joinCanonicalHeaderNames(canonical_headers);

  return fmt::format(IAMRolesAnywhereSigV4SignatureConstants::SigV4AuthorizationHeaderFormat,
                     getAlgorithmName(x509_credentials),
                     createAuthorizationCredential(x509_credentials, credential_scope),
                     signed_headers, signature);
}

absl::string_view
IAMRolesAnywhereSigV4Signer::getAlgorithmName(const X509Credentials& x509_credentials) const {
  return (x509_credentials.publicKeySignatureAlgorithm() ==
          X509Credentials::PublicKeySignatureAlgorithm::RSA)
             ? IAMRolesAnywhereSigV4SignatureConstants::X509SigV4RSA
             : IAMRolesAnywhereSigV4SignatureConstants::X509SigV4ECDSA;
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
