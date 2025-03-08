#include "source/extensions/common/aws/signers/iam_roles_anywhere_sigv4_signer.h"

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
    const X509Credentials x509_credentials, const absl::string_view canonical_request,
    const absl::string_view long_date, const absl::string_view credential_scope) const {
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  if (x509_credentials.publicKeySignatureAlgorithm() ==
      X509Credentials::PublicKeySignatureAlgorithm::RSA) {
    return fmt::format(
        IAMRolesAnywhereSigV4SignatureConstants::SigV4StringToSignFormat,
        IAMRolesAnywhereSigV4SignatureConstants::X509SigV4RSA, long_date, credential_scope,
        Hex::encode(crypto_util.getSha256Digest(Buffer::OwnedImpl(canonical_request))));
  } else {
    return fmt::format(
        IAMRolesAnywhereSigV4SignatureConstants::SigV4StringToSignFormat,
        IAMRolesAnywhereSigV4SignatureConstants::X509SigV4ECDSA, long_date, credential_scope,
        Hex::encode(crypto_util.getSha256Digest(Buffer::OwnedImpl(canonical_request))));
  }
}

std::string
IAMRolesAnywhereSigV4Signer::createSignature(const X509Credentials x509_credentials,
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

std::string IAMRolesAnywhereSigV4Signer::createAuthorizationHeader(
    const X509Credentials x509_credentials, const absl::string_view credential_scope,
    const std::map<std::string, std::string>& canonical_headers,
    const absl::string_view signature) const {

  const auto signed_headers = Utility::joinCanonicalHeaderNames(canonical_headers);

  if (x509_credentials.publicKeySignatureAlgorithm() ==
      X509Credentials::PublicKeySignatureAlgorithm::RSA) {
    return fmt::format(IAMRolesAnywhereSigV4SignatureConstants::SigV4AuthorizationHeaderFormat,
                       IAMRolesAnywhereSigV4SignatureConstants::X509SigV4RSA,
                       createAuthorizationCredential(x509_credentials, credential_scope),
                       signed_headers, signature);

  } else {
    return fmt::format(IAMRolesAnywhereSigV4SignatureConstants::SigV4AuthorizationHeaderFormat,
                       IAMRolesAnywhereSigV4SignatureConstants::X509SigV4ECDSA,
                       createAuthorizationCredential(x509_credentials, credential_scope),
                       signed_headers, signature);
  }
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
