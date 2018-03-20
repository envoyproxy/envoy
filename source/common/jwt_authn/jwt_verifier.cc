#include "common/jwt_authn/jwt.h"

#include "common/common/assert.h"
#include "common/common/base64.h"
#include "common/common/utility.h"
#include "common/json/json_loader.h"
#include "openssl/bn.h"
#include "openssl/ecdsa.h"
#include "openssl/evp.h"
#include "openssl/rsa.h"
#include "openssl/sha.h"

#include <algorithm>
#include <cassert>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace Envoy {
namespace JwtAuthn {

bool Verifier::VerifySignatureRSA(EVP_PKEY *key, const EVP_MD *md,
                                  const uint8_t *signature,
                                  size_t signature_len,
                                  const uint8_t *signed_data,
                                  size_t signed_data_len) {
  bssl::UniquePtr<EVP_MD_CTX> md_ctx(EVP_MD_CTX_create());

  EVP_DigestVerifyInit(md_ctx.get(), nullptr, md, nullptr, key);
  EVP_DigestVerifyUpdate(md_ctx.get(), signed_data, signed_data_len);
  return (EVP_DigestVerifyFinal(md_ctx.get(), signature, signature_len) == 1);
}

bool Verifier::VerifySignatureRSA(EVP_PKEY *key, const EVP_MD *md,
                                  const std::string &signature,
                                  const std::string &signed_data) {
  return VerifySignatureRSA(key, md, CastToUChar(signature), signature.length(),
                            CastToUChar(signed_data), signed_data.length());
}

bool Verifier::VerifySignatureEC(EC_KEY *key, const uint8_t *signature,
                                 size_t signature_len,
                                 const uint8_t *signed_data,
                                 size_t signed_data_len) {
  // ES256 signature should be 64 bytes.
  if (signature_len != 2 * 32) {
    return false;
  }

  uint8_t digest[SHA256_DIGEST_LENGTH];
  SHA256(signed_data, signed_data_len, digest);

  bssl::UniquePtr<ECDSA_SIG> ecdsa_sig(ECDSA_SIG_new());
  if (!ecdsa_sig) {
    UpdateStatus(Status::FAILED_CREATE_ECDSA_SIGNATURE);
    return false;
  }

  BN_bin2bn(signature, 32, ecdsa_sig->r);
  BN_bin2bn(signature + 32, 32, ecdsa_sig->s);
  return (ECDSA_do_verify(digest, SHA256_DIGEST_LENGTH, ecdsa_sig.get(), key) ==
          1);
}

bool Verifier::VerifySignatureEC(EC_KEY *key, const std::string &signature,
                                 const std::string &signed_data) {
  return VerifySignatureEC(key, CastToUChar(signature), signature.length(),
                           CastToUChar(signed_data), signed_data.length());
}

bool Verifier::Verify(const Jwt &jwt, const Pubkeys &pubkeys) {
  // If JWT status is not OK, inherits its status and return false.
  if (jwt.GetStatus() != Status::OK) {
    UpdateStatus(jwt.GetStatus());
    return false;
  }

  // If pubkeys status is not OK, inherits its status and return false.
  if (pubkeys.GetStatus() != Status::OK) {
    UpdateStatus(pubkeys.GetStatus());
    return false;
  }

  std::string signed_data =
      jwt.header_str_base64url_ + '.' + jwt.payload_str_base64url_;
  bool kid_alg_matched = false;
  for (auto &pubkey : pubkeys.keys_) {
    // If kid is specified in JWT, JWK with the same kid is used for
    // verification.
    // If kid is not specified in JWT, try all JWK.
    if (jwt.kid_ != "" && pubkey->kid_specified_ && pubkey->kid_ != jwt.kid_) {
      continue;
    }

    // The same alg must be used.
    if (pubkey->alg_specified_ && pubkey->alg_ != jwt.alg_) {
      continue;
    }
    kid_alg_matched = true;

    if (pubkey->kty_ == "EC" &&
        VerifySignatureEC(pubkey->ec_key_.get(), jwt.signature_, signed_data)) {
      // Verification succeeded.
      return true;
    } else if ((pubkey->pem_format_ || pubkey->kty_ == "RSA") &&
               VerifySignatureRSA(pubkey->evp_pkey_.get(), jwt.md_,
                                  jwt.signature_, signed_data)) {
      // Verification succeeded.
      return true;
    }
  }

  // Verification failed.
  if (kid_alg_matched) {
    UpdateStatus(Status::JWT_INVALID_SIGNATURE);
  } else {
    UpdateStatus(Status::KID_ALG_UNMATCH);
  }
  return false;
}

}  // namespace JwtAuthn
}  // namespace Envoy
