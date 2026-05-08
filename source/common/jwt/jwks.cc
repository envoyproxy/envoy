// Copyright 2018 Google LLC
// Copyright Envoy Project Authors
// SPDX-License-Identifier: Apache-2.0

#include "source/common/jwt/jwks.h"

#include <assert.h>

#include <iostream>

#include "source/common/jwt/struct_utils.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "openssl/bio.h"
#include "openssl/bn.h"
#include "openssl/curve25519.h"
#include "openssl/ecdsa.h"
#include "openssl/evp.h"
#include "openssl/rsa.h"
#include "openssl/sha.h"

namespace Envoy {
namespace JwtVerify {

namespace {

// The x509 certificate prefix string
const char kX509CertPrefix[] = "-----BEGIN CERTIFICATE-----\n";
// The x509 certificate suffix string
const char kX509CertSuffix[] = "\n-----END CERTIFICATE-----\n";

// A convenience inline cast function.
inline const uint8_t* castToUChar(const std::string& str) {
  return reinterpret_cast<const uint8_t*>(str.c_str());
}

/** Class to create key object from string of public key, formatted in PEM
 * or JWKs.
 * If it fails, status_ holds the failure reason.
 *
 * Usage example:
 * KeyGetter e;
 * bssl::UniquePtr<EVP_PKEY> pkey = e.createEcKeyFromJwkEC(...);
 */
class KeyGetter : public WithStatus {
public:
  bssl::UniquePtr<EVP_PKEY> createEvpPkeyFromPem(const std::string& pkey_pem) {
    bssl::UniquePtr<BIO> buf(BIO_new_mem_buf(pkey_pem.data(), pkey_pem.size()));
    if (buf == nullptr) {
      updateStatus(Status::JwksBioAllocError);
      return nullptr;
    }
    bssl::UniquePtr<EVP_PKEY> key(PEM_read_bio_PUBKEY(buf.get(), nullptr, nullptr, nullptr));
    if (key == nullptr) {
      updateStatus(Status::JwksPemBadBase64);
      return nullptr;
    }
    return key;
  }

  bssl::UniquePtr<EC_KEY> createEcKeyFromJwkEC(int nid, const std::string& x,
                                               const std::string& y) {
    bssl::UniquePtr<EC_KEY> ec_key(EC_KEY_new_by_curve_name(nid));
    if (!ec_key) {
      updateStatus(Status::JwksEcCreateKeyFail);
      return nullptr;
    }
    bssl::UniquePtr<BIGNUM> bn_x = createBigNumFromBase64UrlString(x);
    bssl::UniquePtr<BIGNUM> bn_y = createBigNumFromBase64UrlString(y);
    if (!bn_x || !bn_y) {
      // EC public key field x or y Base64 decode fail
      updateStatus(Status::JwksEcXorYBadBase64);
      return nullptr;
    }

    if (EC_KEY_set_public_key_affine_coordinates(ec_key.get(), bn_x.get(), bn_y.get()) == 0) {
      updateStatus(Status::JwksEcParseError);
      return nullptr;
    }
    return ec_key;
  }

  bssl::UniquePtr<RSA> createRsaFromJwk(const std::string& n, const std::string& e) {
    bssl::UniquePtr<BIGNUM> n_bn = createBigNumFromBase64UrlString(n);
    bssl::UniquePtr<BIGNUM> e_bn = createBigNumFromBase64UrlString(e);
    if (n_bn == nullptr || e_bn == nullptr) {
      // RSA public key field is missing or has parse error.
      updateStatus(Status::JwksRsaParseError);
      return nullptr;
    }
    if (BN_cmp_word(e_bn.get(), 3) != 0 && BN_cmp_word(e_bn.get(), 65537) != 0) {
      // non-standard key; reject it early.
      updateStatus(Status::JwksRsaParseError);
      return nullptr;
    }
    // When jwt_verify_lib's minimum supported BoringSSL revision is past
    // https://boringssl-review.googlesource.com/c/boringssl/+/59386 (May 2023),
    // replace all this with `RSA_new_public_key` instead.
    bssl::UniquePtr<RSA> rsa(RSA_new());
    if (rsa == nullptr || !RSA_set0_key(rsa.get(), n_bn.get(), e_bn.get(), /*d=*/nullptr)) {
      // Allocation or programmer error.
      updateStatus(Status::JwksRsaParseError);
      return nullptr;
    }
    // `RSA_set0_key` takes ownership, but only on success.
    n_bn.release();
    e_bn.release();
    if (!RSA_check_key(rsa.get())) {
      // Not a valid RSA public key.
      updateStatus(Status::JwksRsaParseError);
      return nullptr;
    }
    return rsa;
  }

  std::string createRawKeyFromJwkOKP([[maybe_unused]] int nid, size_t keylen,
                                     const std::string& x) {
    std::string x_decoded;
    if (!absl::WebSafeBase64Unescape(x, &x_decoded)) {
      updateStatus(Status::JwksOKPXBadBase64);
    } else if (x_decoded.length() != keylen) {
      updateStatus(Status::JwksOKPXWrongLength);
    }
    // For OKP the "x" value is the public key and can just be used as-is
    return x_decoded;
  }

private:
  bssl::UniquePtr<BIGNUM> createBigNumFromBase64UrlString(const std::string& s) {
    std::string s_decoded;
    if (!absl::WebSafeBase64Unescape(s, &s_decoded)) {
      return nullptr;
    }
    return bssl::UniquePtr<BIGNUM>(BN_bin2bn(castToUChar(s_decoded), s_decoded.length(), NULL));
  };
};

Status extractJwkFromJwkRSA(const Protobuf::Struct& jwk_pb, Jwks::Pubkey* jwk) {
  if (!jwk->alg_.empty() && (jwk->alg_.size() < 2 || (jwk->alg_.compare(0, 2, "RS") != 0 &&
                                                      jwk->alg_.compare(0, 2, "PS") != 0))) {
    return Status::JwksRSAKeyBadAlg;
  }

  StructUtils jwk_getter(jwk_pb);
  std::string n_str;
  auto code = jwk_getter.GetString("n", &n_str);
  if (code == StructUtils::MISSING) {
    return Status::JwksRSAKeyMissingN;
  }
  if (code == StructUtils::WRONG_TYPE) {
    return Status::JwksRSAKeyBadN;
  }

  std::string e_str;
  code = jwk_getter.GetString("e", &e_str);
  if (code == StructUtils::MISSING) {
    return Status::JwksRSAKeyMissingE;
  }
  if (code == StructUtils::WRONG_TYPE) {
    return Status::JwksRSAKeyBadE;
  }

  KeyGetter e;
  jwk->rsa_ = e.createRsaFromJwk(n_str, e_str);
  return e.getStatus();
}

Status extractJwkFromJwkEC(const Protobuf::Struct& jwk_pb, Jwks::Pubkey* jwk) {
  if (!jwk->alg_.empty() && (jwk->alg_.size() < 2 || jwk->alg_.compare(0, 2, "ES") != 0)) {
    return Status::JwksECKeyBadAlg;
  }

  StructUtils jwk_getter(jwk_pb);
  std::string crv_str;
  auto code = jwk_getter.GetString("crv", &crv_str);
  if (code == StructUtils::MISSING) {
    crv_str = "";
  }
  if (code == StructUtils::WRONG_TYPE) {
    return Status::JwksECKeyBadCrv;
  }
  jwk->crv_ = crv_str;

  // If both alg and crv specified, make sure they match
  if (!jwk->alg_.empty() && !jwk->crv_.empty()) {
    if (!((jwk->alg_ == "ES256" && jwk->crv_ == "P-256") ||
          (jwk->alg_ == "ES384" && jwk->crv_ == "P-384") ||
          (jwk->alg_ == "ES512" && jwk->crv_ == "P-521"))) {
      return Status::JwksECKeyAlgNotCompatibleWithCrv;
    }
  }

  // If neither alg or crv is set, assume P-256
  if (jwk->alg_.empty() && jwk->crv_.empty()) {
    jwk->crv_ = "P-256";
  }

  int nid;
  if (jwk->alg_ == "ES256" || jwk->crv_ == "P-256") {
    nid = NID_X9_62_prime256v1;
    jwk->crv_ = "P-256";
  } else if (jwk->alg_ == "ES384" || jwk->crv_ == "P-384") {
    nid = NID_secp384r1;
    jwk->crv_ = "P-384";
  } else if (jwk->alg_ == "ES512" || jwk->crv_ == "P-521") {
    nid = NID_secp521r1;
    jwk->crv_ = "P-521";
  } else {
    return Status::JwksECKeyAlgOrCrvUnsupported;
  }

  std::string x_str;
  code = jwk_getter.GetString("x", &x_str);
  if (code == StructUtils::MISSING) {
    return Status::JwksECKeyMissingX;
  }
  if (code == StructUtils::WRONG_TYPE) {
    return Status::JwksECKeyBadX;
  }

  std::string y_str;
  code = jwk_getter.GetString("y", &y_str);
  if (code == StructUtils::MISSING) {
    return Status::JwksECKeyMissingY;
  }
  if (code == StructUtils::WRONG_TYPE) {
    return Status::JwksECKeyBadY;
  }

  KeyGetter e;
  jwk->ec_key_ = e.createEcKeyFromJwkEC(nid, x_str, y_str);
  return e.getStatus();
}

Status extractJwkFromJwkOct(const Protobuf::Struct& jwk_pb, Jwks::Pubkey* jwk) {
  if (!jwk->alg_.empty() && jwk->alg_ != "HS256" && jwk->alg_ != "HS384" && jwk->alg_ != "HS512") {
    return Status::JwksHMACKeyBadAlg;
  }

  StructUtils jwk_getter(jwk_pb);
  std::string k_str;
  auto code = jwk_getter.GetString("k", &k_str);
  if (code == StructUtils::MISSING) {
    return Status::JwksHMACKeyMissingK;
  }
  if (code == StructUtils::WRONG_TYPE) {
    return Status::JwksHMACKeyBadK;
  }

  std::string key;
  if (!absl::WebSafeBase64Unescape(k_str, &key) || key.empty()) {
    return Status::JwksOctBadBase64;
  }

  jwk->hmac_key_ = key;
  return Status::Ok;
}

// The "OKP" key type is defined in https://tools.ietf.org/html/rfc8037
Status extractJwkFromJwkOKP(const Protobuf::Struct& jwk_pb, Jwks::Pubkey* jwk) {
  // alg is not required, but if present it must be EdDSA
  if (!jwk->alg_.empty() && jwk->alg_ != "EdDSA") {
    return Status::JwksOKPKeyBadAlg;
  }

  // crv is required per https://tools.ietf.org/html/rfc8037#section-2
  StructUtils jwk_getter(jwk_pb);
  std::string crv_str;
  auto code = jwk_getter.GetString("crv", &crv_str);
  if (code == StructUtils::MISSING) {
    return Status::JwksOKPKeyMissingCrv;
  }
  if (code == StructUtils::WRONG_TYPE) {
    return Status::JwksOKPKeyBadCrv;
  }
  jwk->crv_ = crv_str;

  // Valid crv values:
  // https://tools.ietf.org/html/rfc8037#section-3
  // https://www.iana.org/assignments/jose/jose.xhtml#web-key-elliptic-curve
  // In addition to Ed25519 there are:
  // X25519: Implemented in boringssl but not used for JWT and thus not
  // supported here
  // Ed448 and X448: Not implemented in boringssl
  int nid;
  size_t keylen;
  if (jwk->crv_ == "Ed25519") {
    nid = EVP_PKEY_ED25519;
    keylen = ED25519_PUBLIC_KEY_LEN;
  } else {
    return Status::JwksOKPKeyCrvUnsupported;
  }

  // x is required per https://tools.ietf.org/html/rfc8037#section-2
  std::string x_str;
  code = jwk_getter.GetString("x", &x_str);
  if (code == StructUtils::MISSING) {
    return Status::JwksOKPKeyMissingX;
  }
  if (code == StructUtils::WRONG_TYPE) {
    return Status::JwksOKPKeyBadX;
  }

  KeyGetter e;
  jwk->okp_key_raw_ = e.createRawKeyFromJwkOKP(nid, keylen, x_str);
  return e.getStatus();
}

Status extractJwk(const Protobuf::Struct& jwk_pb, Jwks::Pubkey* jwk) {
  StructUtils jwk_getter(jwk_pb);
  // Check "kty" parameter, it should exist.
  // https://tools.ietf.org/html/rfc7517#section-4.1
  auto code = jwk_getter.GetString("kty", &jwk->kty_);
  if (code == StructUtils::MISSING) {
    return Status::JwksMissingKty;
  }
  if (code == StructUtils::WRONG_TYPE) {
    return Status::JwksBadKty;
  }

  // "kid" and "alg" are optional, if they do not exist, set them to
  // empty. https://tools.ietf.org/html/rfc7517#page-8
  jwk_getter.GetString("kid", &jwk->kid_);
  jwk_getter.GetString("alg", &jwk->alg_);

  // Extract public key according to "kty" value.
  // https://tools.ietf.org/html/rfc7518#section-6.1
  if (jwk->kty_ == "EC") {
    return extractJwkFromJwkEC(jwk_pb, jwk);
  } else if (jwk->kty_ == "RSA") {
    return extractJwkFromJwkRSA(jwk_pb, jwk);
  } else if (jwk->kty_ == "oct") {
    return extractJwkFromJwkOct(jwk_pb, jwk);
  } else if (jwk->kty_ == "OKP") {
    return extractJwkFromJwkOKP(jwk_pb, jwk);
  }
  return Status::JwksNotImplementedKty;
}

Status extractX509(const std::string& key, Jwks::Pubkey* jwk) {
  jwk->bio_.reset(BIO_new(BIO_s_mem()));
  if (BIO_write(jwk->bio_.get(), key.c_str(), key.length()) <= 0) {
    return Status::JwksX509BioWriteError;
  }
  jwk->x509_.reset(PEM_read_bio_X509(jwk->bio_.get(), nullptr, nullptr, nullptr));
  if (jwk->x509_ == nullptr) {
    return Status::JwksX509ParseError;
  }
  bssl::UniquePtr<EVP_PKEY> tmp_pkey(X509_get_pubkey(jwk->x509_.get()));
  if (tmp_pkey == nullptr) {
    return Status::JwksX509GetPubkeyError;
  }
  jwk->rsa_.reset(EVP_PKEY_get1_RSA(tmp_pkey.get()));
  if (jwk->rsa_ == nullptr) {
    return Status::JwksX509GetPubkeyError;
  }
  return Status::Ok;
}

bool shouldCheckX509(const Protobuf::Struct& jwks_pb) {
  if (jwks_pb.fields().empty()) {
    return false;
  }

  for (const auto& kid : jwks_pb.fields()) {
    if (kid.first.empty() || kid.second.kind_case() != Protobuf::Value::kStringValue) {
      return false;
    }
    const std::string& cert = kid.second.string_value();
    if (!absl::StartsWith(cert, kX509CertPrefix) || !absl::EndsWith(cert, kX509CertSuffix)) {
      return false;
    }
  }
  return true;
}

Status createFromX509(const Protobuf::Struct& jwks_pb, std::vector<Jwks::PubkeyPtr>& keys) {
  for (const auto& kid : jwks_pb.fields()) {
    Jwks::PubkeyPtr key_ptr(new Jwks::Pubkey());
    Status status = extractX509(kid.second.string_value(), key_ptr.get());
    if (status != Status::Ok) {
      return status;
    }

    key_ptr->kid_ = kid.first;
    key_ptr->kty_ = "RSA";
    keys.push_back(std::move(key_ptr));
  }
  return Status::Ok;
}

} // namespace

Status Jwks::addKeyFromPem(const std::string& pkey, const std::string& kid,
                           const std::string& alg) {
  JwksPtr tmp = Jwks::createFromPem(pkey, kid, alg);
  if (tmp->getStatus() != Status::Ok) {
    return tmp->getStatus();
  }
  keys_.insert(keys_.end(), std::make_move_iterator(tmp->keys_.begin()),
               std::make_move_iterator(tmp->keys_.end()));
  return Status::Ok;
}

JwksPtr Jwks::createFrom(const std::string& pkey, Type type) {
  JwksPtr keys(new Jwks());
  switch (type) {
  case Type::JWKS:
    keys->createFromJwksCore(pkey);
    break;
  case Type::PEM:
    keys->createFromPemCore(pkey);
    break;
  }
  return keys;
}

JwksPtr Jwks::createFromPem(const std::string& pkey, const std::string& kid,
                            const std::string& alg) {
  std::unique_ptr<Jwks> ret = Jwks::createFrom(pkey, Jwks::PEM);
  if (ret->getStatus() != Status::Ok) {
    return ret;
  }
  if (ret->keys_.size() != 1) {
    ret->updateStatus(Status::JwksPemBadBase64);
    return ret;
  }
  Pubkey* jwk = ret->keys_.at(0).get();
  jwk->kid_ = kid;
  jwk->alg_ = alg;

  // If alg is a known EC algorithm, set the correct crv as well.
  if (jwk->alg_ == "ES256") {
    jwk->crv_ = "P-256";
  }
  if (jwk->alg_ == "ES384") {
    jwk->crv_ = "P-384";
  }
  if (jwk->alg_ == "ES512") {
    jwk->crv_ = "P-521";
  }
  return ret;
}

// pkey_pem must be a PEM-encoded PKCS #8 public key.
// This is the format that starts with -----BEGIN PUBLIC KEY-----.
void Jwks::createFromPemCore(const std::string& pkey_pem) {
  keys_.clear();
  PubkeyPtr key_ptr(new Pubkey());
  KeyGetter e;
  bssl::UniquePtr<EVP_PKEY> evp_pkey(e.createEvpPkeyFromPem(pkey_pem));
  updateStatus(e.getStatus());

  if (evp_pkey == nullptr) {
    assert(e.getStatus() != Status::Ok);
    return;
  }
  assert(e.getStatus() == Status::Ok);

  switch (EVP_PKEY_id(evp_pkey.get())) {
  case EVP_PKEY_RSA:
    key_ptr->rsa_.reset(EVP_PKEY_get1_RSA(evp_pkey.get()));
    key_ptr->kty_ = "RSA";
    break;
  case EVP_PKEY_EC:
    key_ptr->ec_key_.reset(EVP_PKEY_get1_EC_KEY(evp_pkey.get()));
    key_ptr->kty_ = "EC";
    break;
#ifndef BORINGSSL_FIPS
  case EVP_PKEY_ED25519: {
    uint8_t raw_key[ED25519_PUBLIC_KEY_LEN];
    size_t out_len = ED25519_PUBLIC_KEY_LEN;
    if (EVP_PKEY_get_raw_public_key(evp_pkey.get(), raw_key, &out_len) != 1 ||
        out_len != ED25519_PUBLIC_KEY_LEN) {
      updateStatus(Status::JwksPemGetRawEd25519Error);
      return;
    }
    key_ptr->okp_key_raw_ = std::string(reinterpret_cast<const char*>(raw_key), out_len);
    key_ptr->kty_ = "OKP";
    key_ptr->crv_ = "Ed25519";
    break;
  }
#endif
  default:
    updateStatus(Status::JwksPemNotImplementedKty);
    return;
  }

  keys_.push_back(std::move(key_ptr));
}

void Jwks::createFromJwksCore(const std::string& jwks_json) {
  keys_.clear();

  Protobuf::util::JsonParseOptions options;
  Protobuf::Struct jwks_pb;
  const auto status = Protobuf::util::JsonStringToMessage(jwks_json, &jwks_pb, options);
  if (!status.ok()) {
    updateStatus(Status::JwksParseError);
    return;
  }

  const auto& fields = jwks_pb.fields();
  const auto keys_it = fields.find("keys");
  if (keys_it == fields.end()) {
    // X509 doesn't have "keys" field.
    if (shouldCheckX509(jwks_pb)) {
      updateStatus(createFromX509(jwks_pb, keys_));
      return;
    }
    updateStatus(Status::JwksNoKeys);
    return;
  }
  if (keys_it->second.kind_case() != Protobuf::Value::kListValue) {
    updateStatus(Status::JwksBadKeys);
    return;
  }

  for (const auto& key_value : keys_it->second.list_value().values()) {
    if (key_value.kind_case() != Protobuf::Value::kStructValue) {
      continue;
    }
    PubkeyPtr key_ptr(new Pubkey());
    Status status = extractJwk(key_value.struct_value(), key_ptr.get());
    if (status == Status::Ok) {
      keys_.push_back(std::move(key_ptr));
      resetStatus(status);
    } else {
      updateStatus(status);
    }
  }

  if (keys_.empty()) {
    updateStatus(Status::JwksNoValidKeys);
  } else {
    resetStatus(Status::Ok);
  }
}

} // namespace JwtVerify
} // namespace Envoy
