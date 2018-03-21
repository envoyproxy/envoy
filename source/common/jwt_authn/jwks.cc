#include "common/jwt_authn/jwks.h"

#include "common/common/assert.h"
#include "common/common/base64.h"
#include "common/json/json_loader.h"
#include "common/jwt_authn/utils.h"

#include "openssl/bn.h"
#include "openssl/ecdsa.h"
#include "openssl/evp.h"
#include "openssl/rsa.h"
#include "openssl/sha.h"

namespace Envoy {
namespace JwtAuthn {

namespace {

// Class to create EVP_PKEY object from string of public key, formatted in PEM
// or JWKs.
// If it failed, status_ holds the failure reason.
//
// Usage example:
//   EvpPkeyGetter e;
//   bssl::UniquePtr<EVP_PKEY> pkey =
//   e.EvpPkeyFromStr(pem_formatted_public_key);
// (You can use EvpPkeyFromJwkRSA() or EcKeyFromJwkEC() for JWKs)
class EvpPkeyGetter : public WithStatus {
public:
  EvpPkeyGetter() {}

  bssl::UniquePtr<EVP_PKEY> EvpPkeyFromStr(const std::string& pkey_pem) {
    std::string pkey_der = Base64::decode(pkey_pem);
    if (pkey_der == "") {
      UpdateStatus(Status::PEM_PUBKEY_BAD_BASE64);
      return nullptr;
    }
    auto rsa =
        bssl::UniquePtr<RSA>(RSA_public_key_from_bytes(CastToUChar(pkey_der), pkey_der.length()));
    if (!rsa) {
      UpdateStatus(Status::PEM_PUBKEY_PARSE_ERROR);
    }
    return EvpPkeyFromRsa(rsa.get());
  }

  bssl::UniquePtr<EVP_PKEY> EvpPkeyFromJwkRSA(const std::string& n, const std::string& e) {
    return EvpPkeyFromRsa(RsaFromJwk(n, e).get());
  }

  bssl::UniquePtr<EC_KEY> EcKeyFromJwkEC(const std::string& x, const std::string& y) {
    bssl::UniquePtr<EC_KEY> ec_key(EC_KEY_new_by_curve_name(NID_X9_62_prime256v1));
    if (!ec_key) {
      UpdateStatus(Status::FAILED_CREATE_EC_KEY);
      return nullptr;
    }
    bssl::UniquePtr<BIGNUM> bn_x = BigNumFromBase64UrlString(x);
    bssl::UniquePtr<BIGNUM> bn_y = BigNumFromBase64UrlString(y);
    if (!bn_x || !bn_y) {
      // EC public key field is missing or has parse error.
      UpdateStatus(Status::JWK_EC_PUBKEY_PARSE_ERROR);
      return nullptr;
    }

    if (EC_KEY_set_public_key_affine_coordinates(ec_key.get(), bn_x.get(), bn_y.get()) == 0) {
      UpdateStatus(Status::JWK_EC_PUBKEY_PARSE_ERROR);
      return nullptr;
    }
    return ec_key;
  }

private:
  // In the case where rsa is nullptr, UpdateStatus() should be called
  // appropriately elsewhere.
  bssl::UniquePtr<EVP_PKEY> EvpPkeyFromRsa(RSA* rsa) {
    if (!rsa) {
      return nullptr;
    }
    bssl::UniquePtr<EVP_PKEY> key(EVP_PKEY_new());
    EVP_PKEY_set1_RSA(key.get(), rsa);
    return key;
  }

  bssl::UniquePtr<BIGNUM> BigNumFromBase64UrlString(const std::string& s) {
    std::string s_decoded = Base64UrlDecode(s);
    if (s_decoded == "") {
      return nullptr;
    }
    return bssl::UniquePtr<BIGNUM>(BN_bin2bn(CastToUChar(s_decoded), s_decoded.length(), NULL));
  };

  bssl::UniquePtr<RSA> RsaFromJwk(const std::string& n, const std::string& e) {
    bssl::UniquePtr<RSA> rsa(RSA_new());
    // It crash if RSA object couldn't be created.
    ASSERT(rsa);

    rsa->n = BigNumFromBase64UrlString(n).release();
    rsa->e = BigNumFromBase64UrlString(e).release();
    if (!rsa->n || !rsa->e) {
      // RSA public key field is missing or has parse error.
      UpdateStatus(Status::JWK_RSA_PUBKEY_PARSE_ERROR);
      return nullptr;
    }
    return rsa;
  }
};

} // namespace

std::unique_ptr<Jwks> Jwks::CreateFrom(const std::string& pkey, Type type) {
  std::unique_ptr<Jwks> keys(new Jwks());
  switch (type) {
  case Type::JWKS:
    keys->CreateFromJwksCore(pkey);
    break;
  case Type::PEM:
    keys->CreateFromPemCore(pkey);
    break;
  default:
    PANIC("can not reach here");
  }
  return keys;
}

void Jwks::CreateFromPemCore(const std::string& pkey_pem) {
  keys_.clear();
  std::unique_ptr<Jwk> key_ptr(new Jwk());
  EvpPkeyGetter e;
  key_ptr->evp_pkey = e.EvpPkeyFromStr(pkey_pem);
  key_ptr->pem_format = true;
  UpdateStatus(e.GetStatus());
  if (e.GetStatus() == Status::OK) {
    keys_.push_back(std::move(key_ptr));
  }
}

void Jwks::CreateFromJwksCore(const std::string& pkey_jwks) {
  keys_.clear();

  Json::ObjectSharedPtr jwks_json;
  try {
    jwks_json = Json::Factory::loadFromString(pkey_jwks);
  } catch (Json::Exception& e) {
    UpdateStatus(Status::JWK_PARSE_ERROR);
    return;
  }
  std::vector<Json::ObjectSharedPtr> keys;
  if (!jwks_json->hasObject("keys")) {
    UpdateStatus(Status::JWK_NO_KEYS);
    return;
  }
  try {
    keys = jwks_json->getObjectArray("keys", true);
  } catch (Json::Exception& e) {
    UpdateStatus(Status::JWK_BAD_KEYS);
    return;
  }

  for (auto jwk_json : keys) {
    try {
      ExtractJwk(jwk_json);
    } catch (Json::Exception& e) {
      continue;
    }
  }

  if (keys_.size() == 0) {
    UpdateStatus(Status::JWK_NO_VALID_PUBKEY);
  }
}

void Jwks::ExtractJwk(Json::ObjectSharedPtr jwk_json) {
  // Check "kty" parameter, it should exist.
  // https://tools.ietf.org/html/rfc7517#section-4.1
  // If "kty" is missing, getString throws an exception.
  std::string kty = jwk_json->getString("kty");

  // Extract public key according to "kty" value.
  // https://tools.ietf.org/html/rfc7518#section-6.1
  if (kty == "EC") {
    ExtractJwkFromJwkEC(jwk_json);
  } else if (kty == "RSA") {
    ExtractJwkFromJwkRSA(jwk_json);
  }
}

void Jwks::ExtractJwkFromJwkRSA(Json::ObjectSharedPtr jwk_json) {
  std::unique_ptr<Jwk> jwk(new Jwk());
  std::string n_str, e_str;
  try {
    // "kid" and "alg" are optional, if they do not exist, set them to "".
    // https://tools.ietf.org/html/rfc7517#page-8
    if (jwk_json->hasObject("kid")) {
      jwk->kid = jwk_json->getString("kid");
      jwk->kid_specified = true;
    }
    if (jwk_json->hasObject("alg")) {
      jwk->alg = jwk_json->getString("alg");
      if (jwk->alg.compare(0, 2, "RS") != 0) {
        return;
      }
      jwk->alg_specified = true;
    }
    jwk->kty = jwk_json->getString("kty");
    n_str = jwk_json->getString("n");
    e_str = jwk_json->getString("e");
  } catch (Json::Exception& e) {
    // Do not extract public key if jwk_json has bad format.
    return;
  }

  EvpPkeyGetter e;
  jwk->evp_pkey = e.EvpPkeyFromJwkRSA(n_str, e_str);
  UpdateStatus(e.GetStatus());
  if (e.GetStatus() == Status::OK) {
    keys_.push_back(std::move(jwk));
  }
}

void Jwks::ExtractJwkFromJwkEC(Json::ObjectSharedPtr jwk_json) {
  std::unique_ptr<Jwk> jwk(new Jwk());
  std::string x_str, y_str;
  try {
    // "kid" and "alg" are optional, if they do not exist, set them to "".
    // https://tools.ietf.org/html/rfc7517#page-8
    if (jwk_json->hasObject("kid")) {
      jwk->kid = jwk_json->getString("kid");
      jwk->kid_specified = true;
    }
    if (jwk_json->hasObject("alg")) {
      jwk->alg = jwk_json->getString("alg");
      if (jwk->alg != "ES256") {
        return;
      }
      jwk->alg_specified = true;
    }
    jwk->kty = jwk_json->getString("kty");
    x_str = jwk_json->getString("x");
    y_str = jwk_json->getString("y");
  } catch (Json::Exception& e) {
    // Do not extract public key if jwk_json has bad format.
    return;
  }

  EvpPkeyGetter e;
  jwk->ec_key = e.EcKeyFromJwkEC(x_str, y_str);
  UpdateStatus(e.GetStatus());
  if (e.GetStatus() == Status::OK) {
    keys_.push_back(std::move(jwk));
  }
}

} // namespace JwtAuthn
} // namespace Envoy
