#pragma once

#include <string>
#include <vector>

#include "envoy/json/json_object.h"

#include "common/jwt_authn/status.h"

#include "openssl/ec.h"
#include "openssl/evp.h"

namespace Envoy {
namespace JwtAuthn {

/**
 *  Class to parse and a hold JSON Web Key Set.
 *
 *  Usage example:
 *  std::unique_ptr<Jwks> keys = Jwks::CreateFrom(jwks_string);
 *  if (keys->getStatus() == Status::Ok) { ... }
 */
class Jwks : public WithStatus {
public:
  // Format of public key.
  enum Type { PEM, JWKS };

  // Create from string
  static std::unique_ptr<Jwks> createFrom(const std::string& pkey, Type type);

  // Struct for JSON Web Key
  struct Pubkey {
    bssl::UniquePtr<EVP_PKEY> evp_pkey_;
    bssl::UniquePtr<EC_KEY> ec_key_;
    std::string kid_;
    std::string kty_;
    std::string alg_;
    bool alg_specified_ = false;
    bool kid_specified_ = false;
    bool pem_format_ = false;
  };
  typedef std::unique_ptr<Pubkey> PubkeyPtr;

  // Access to list of Jwks
  const std::vector<PubkeyPtr>& keys() const { return keys_; }

private:
  // Create Pem
  void createFromPemCore(const std::string& pkey_pem);
  // Create Jwks
  void createFromJwksCore(const std::string& pkey_jwks);

  // Extracts the public key from a jwk key (jkey) and sets it to keys_;
  void extractJwk(Json::ObjectSharedPtr jwk_json);
  // Create RSA Jwk
  void extractJwkFromJwkRSA(Json::ObjectSharedPtr jwk_json);
  // Create EC Jwk
  void extractJwkFromJwkEC(Json::ObjectSharedPtr jwk_json);

  // List of Jwks
  std::vector<PubkeyPtr> keys_;
};

typedef std::unique_ptr<Jwks> JwksPtr;

} // namespace JwtAuthn
} // namespace Envoy
