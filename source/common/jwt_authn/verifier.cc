#include "common/jwt_authn/utils.h"
#include "common/jwt_authn/verifier.h"

#include "openssl/bn.h"
#include "openssl/ecdsa.h"
#include "openssl/evp.h"
#include "openssl/rsa.h"
#include "openssl/sha.h"

namespace Envoy {
namespace JwtAuthn {
namespace {

bool VerifySignatureRSA(EVP_PKEY* key, const EVP_MD* md, const uint8_t* signature,
                        size_t signature_len, const uint8_t* signed_data, size_t signed_data_len) {
  bssl::UniquePtr<EVP_MD_CTX> md_ctx(EVP_MD_CTX_create());

  EVP_DigestVerifyInit(md_ctx.get(), nullptr, md, nullptr, key);
  EVP_DigestVerifyUpdate(md_ctx.get(), signed_data, signed_data_len);
  return (EVP_DigestVerifyFinal(md_ctx.get(), signature, signature_len) == 1);
}

bool VerifySignatureRSA(EVP_PKEY* key, const EVP_MD* md, const std::string& signature,
                        const std::string& signed_data) {
  return VerifySignatureRSA(key, md, CastToUChar(signature), signature.length(),
                            CastToUChar(signed_data), signed_data.length());
}

bool VerifySignatureEC(EC_KEY* key, const uint8_t* signature, size_t signature_len,
                       const uint8_t* signed_data, size_t signed_data_len) {
  // ES256 signature should be 64 bytes.
  if (signature_len != 2 * 32) {
    return false;
  }

  uint8_t digest[SHA256_DIGEST_LENGTH];
  SHA256(signed_data, signed_data_len, digest);

  bssl::UniquePtr<ECDSA_SIG> ecdsa_sig(ECDSA_SIG_new());
  if (!ecdsa_sig) {
    return false;
  }

  BN_bin2bn(signature, 32, ecdsa_sig->r);
  BN_bin2bn(signature + 32, 32, ecdsa_sig->s);
  return (ECDSA_do_verify(digest, SHA256_DIGEST_LENGTH, ecdsa_sig.get(), key) == 1);
}

bool VerifySignatureEC(EC_KEY* key, const std::string& signature, const std::string& signed_data) {
  return VerifySignatureEC(key, CastToUChar(signature), signature.length(),
                           CastToUChar(signed_data), signed_data.length());
}

} // namespace

Status VerifyJwt(const Jwt& jwt, const Jwks& jwks) {
  std::string signed_data = jwt.header_str_base64url + '.' + jwt.payload_str_base64url;
  bool kid_alg_matched = false;
  for (auto& jwk : jwks.keys()) {
    // If kid is specified in JWT, JWK with the same kid is used for
    // verification.
    // If kid is not specified in JWT, try all JWK.
    if (jwt.kid != "" && jwk->kid_specified && jwk->kid != jwt.kid) {
      continue;
    }

    // The same alg must be used.
    if (jwk->alg_specified && jwk->alg != jwt.alg) {
      continue;
    }
    kid_alg_matched = true;

    if (jwk->kty == "EC" && VerifySignatureEC(jwk->ec_key.get(), jwt.signature, signed_data)) {
      // Verification succeeded.
      return Status::OK;
    } else if ((jwk->pem_format || jwk->kty == "RSA") &&
               VerifySignatureRSA(jwk->evp_pkey.get(), EVP_sha256(), jwt.signature, signed_data)) {
      // Verification succeeded.
      return Status::OK;
    }
  }

  // Verification failed.
  return kid_alg_matched ? Status::JWT_INVALID_SIGNATURE : Status::KID_ALG_UNMATCH;
}

} // namespace JwtAuthn
} // namespace Envoy
