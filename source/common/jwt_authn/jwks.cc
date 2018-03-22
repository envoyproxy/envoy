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

/** Class to create EVP_PKEY object from string of public key, formatted in PEM or JWKs.
 * If it failed, status_ holds the failure reason.
 *
 * Usage example:
 * EvpPkeyGetter e;
 * bssl::UniquePtr<EVP_PKEY> pkey = e.createEvpPkeyFromStr(pem_formatted_public_key);
 * (You can use createEvpPkeyFromJwkRSA() or createEcKeyFromJwkEC() for JWKs)
 */
class EvpPkeyGetter : public WithStatus {
public:
  // Create EVP_PKEY from PEM string
  bssl::UniquePtr<EVP_PKEY> createEvpPkeyFromStr(const std::string& pkey_pem) {
    std::string pkey_der = Base64::decode(pkey_pem);
    if (pkey_der == "") {
      updateStatus(Status::JwksPemBadBase64);
      return nullptr;
    }
    auto rsa =
        bssl::UniquePtr<RSA>(RSA_public_key_from_bytes(castToUChar(pkey_der), pkey_der.length()));
    if (!rsa) {
      updateStatus(Status::JwksPemParseError);
    }
    return createEvpPkeyFromRsa(rsa.get());
  }

  bssl::UniquePtr<EVP_PKEY> createEvpPkeyFromJwkRSA(const std::string& n, const std::string& e) {
    return createEvpPkeyFromRsa(createRsaFromJwk(n, e).get());
  }

  bssl::UniquePtr<EC_KEY> createEcKeyFromJwkEC(const std::string& x, const std::string& y) {
    bssl::UniquePtr<EC_KEY> ec_key(EC_KEY_new_by_curve_name(NID_X9_62_prime256v1));
    if (!ec_key) {
      updateStatus(Status::JwksEcCreateKeyFail);
      return nullptr;
    }
    bssl::UniquePtr<BIGNUM> bn_x = createBigNumFromBase64UrlString(x);
    bssl::UniquePtr<BIGNUM> bn_y = createBigNumFromBase64UrlString(y);
    if (!bn_x || !bn_y) {
      // EC public key field is missing or has parse error.
      updateStatus(Status::JwksEcParseError);
      return nullptr;
    }

    if (EC_KEY_set_public_key_affine_coordinates(ec_key.get(), bn_x.get(), bn_y.get()) == 0) {
      updateStatus(Status::JwksEcParseError);
      return nullptr;
    }
    return ec_key;
  }

private:
  bssl::UniquePtr<EVP_PKEY> createEvpPkeyFromRsa(RSA* rsa) {
    if (!rsa) {
      return nullptr;
    }
    bssl::UniquePtr<EVP_PKEY> key(EVP_PKEY_new());
    EVP_PKEY_set1_RSA(key.get(), rsa);
    return key;
  }

  bssl::UniquePtr<BIGNUM> createBigNumFromBase64UrlString(const std::string& s) {
    std::string s_decoded = decodeBase64Url(s);
    if (s_decoded == "") {
      return nullptr;
    }
    return bssl::UniquePtr<BIGNUM>(BN_bin2bn(castToUChar(s_decoded), s_decoded.length(), NULL));
  };

  bssl::UniquePtr<RSA> createRsaFromJwk(const std::string& n, const std::string& e) {
    bssl::UniquePtr<RSA> rsa(RSA_new());
    // It crash if RSA object couldn't be created.
    ASSERT(rsa);

    rsa->n = createBigNumFromBase64UrlString(n).release();
    rsa->e = createBigNumFromBase64UrlString(e).release();
    if (!rsa->n || !rsa->e) {
      // RSA public key field is missing or has parse error.
      updateStatus(Status::JwksRsaParseError);
      return nullptr;
    }
    return rsa;
  }
};

} // namespace

std::unique_ptr<Jwks> Jwks::createFrom(const std::string& pkey, Type type) {
  std::unique_ptr<Jwks> keys(new Jwks());
  switch (type) {
  case Type::JWKS:
    keys->createFromJwksCore(pkey);
    break;
  case Type::PEM:
    keys->createFromPemCore(pkey);
    break;
  default:
    PANIC("can not reach here");
  }
  return keys;
}

void Jwks::createFromPemCore(const std::string& pkey_pem) {
  keys_.clear();
  std::unique_ptr<Pubkey> key_ptr(new Pubkey());
  EvpPkeyGetter e;
  key_ptr->evp_pkey_ = e.createEvpPkeyFromStr(pkey_pem);
  key_ptr->pem_format_ = true;
  updateStatus(e.getStatus());
  if (e.getStatus() == Status::Ok) {
    keys_.push_back(std::move(key_ptr));
  }
}

void Jwks::createFromJwksCore(const std::string& pkey_jwks) {
  keys_.clear();

  Json::ObjectSharedPtr jwks_json;
  try {
    jwks_json = Json::Factory::loadFromString(pkey_jwks);
  } catch (Json::Exception& e) {
    updateStatus(Status::JwksParseError);
    return;
  }
  std::vector<Json::ObjectSharedPtr> keys;
  if (!jwks_json->hasObject("keys")) {
    updateStatus(Status::JwksNoKeys);
    return;
  }
  try {
    keys = jwks_json->getObjectArray("keys", true);
  } catch (Json::Exception& e) {
    updateStatus(Status::JwksBadKeys);
    return;
  }

  for (auto jwk_json : keys) {
    try {
      extractJwk(jwk_json);
    } catch (Json::Exception& e) {
      continue;
    }
  }

  if (keys_.size() == 0) {
    updateStatus(Status::JwksNoValidKeys);
  }
}

void Jwks::extractJwk(Json::ObjectSharedPtr jwk_json) {
  // Check "kty" parameter, it should exist.
  // https://tools.ietf.org/html/rfc7517#section-4.1
  // If "kty" is missing, getString throws an exception.
  std::string kty = jwk_json->getString("kty");

  // Extract public key according to "kty" value.
  // https://tools.ietf.org/html/rfc7518#section-6.1
  if (kty == "EC") {
    extractJwkFromJwkEC(jwk_json);
  } else if (kty == "RSA") {
    extractJwkFromJwkRSA(jwk_json);
  }
}

void Jwks::extractJwkFromJwkRSA(Json::ObjectSharedPtr jwk_json) {
  std::unique_ptr<Pubkey> jwk(new Pubkey());
  std::string n_str, e_str;
  try {
    // "kid" and "alg" are optional, if they do not exist, set them to "".
    // https://tools.ietf.org/html/rfc7517#page-8
    if (jwk_json->hasObject("kid")) {
      jwk->kid_ = jwk_json->getString("kid");
      jwk->kid_specified_ = true;
    }
    if (jwk_json->hasObject("alg")) {
      jwk->alg_ = jwk_json->getString("alg");
      if (jwk->alg_.compare(0, 2, "RS") != 0) {
        return;
      }
      jwk->alg_specified_ = true;
    }
    jwk->kty_ = jwk_json->getString("kty");
    n_str = jwk_json->getString("n");
    e_str = jwk_json->getString("e");
  } catch (Json::Exception& e) {
    // Do not extract public key if jwk_json has bad format.
    return;
  }

  EvpPkeyGetter e;
  jwk->evp_pkey_ = e.createEvpPkeyFromJwkRSA(n_str, e_str);
  updateStatus(e.getStatus());
  if (e.getStatus() == Status::Ok) {
    keys_.push_back(std::move(jwk));
  }
}

void Jwks::extractJwkFromJwkEC(Json::ObjectSharedPtr jwk_json) {
  std::unique_ptr<Pubkey> jwk(new Pubkey());
  std::string x_str, y_str;
  try {
    // "kid" and "alg" are optional, if they do not exist, set them to "".
    // https://tools.ietf.org/html/rfc7517#page-8
    if (jwk_json->hasObject("kid")) {
      jwk->kid_ = jwk_json->getString("kid");
      jwk->kid_specified_ = true;
    }
    if (jwk_json->hasObject("alg")) {
      jwk->alg_ = jwk_json->getString("alg");
      if (jwk->alg_ != "ES256") {
        return;
      }
      jwk->alg_specified_ = true;
    }
    jwk->kty_ = jwk_json->getString("kty");
    x_str = jwk_json->getString("x");
    y_str = jwk_json->getString("y");
  } catch (Json::Exception& e) {
    // Do not extract public key if jwk_json has bad format.
    return;
  }

  EvpPkeyGetter e;
  jwk->ec_key_ = e.createEcKeyFromJwkEC(x_str, y_str);
  updateStatus(e.getStatus());
  if (e.getStatus() == Status::Ok) {
    keys_.push_back(std::move(jwk));
  }
}

} // namespace JwtAuthn
} // namespace Envoy
