#pragma once

#include <string>
#include <vector>

#include "envoy/json/json_object.h"

#include "common/jwt_authn/status.h"

#include "openssl/ec.h"
#include "openssl/evp.h"

namespace Envoy {
namespace JwtAuthn {

// Class to parse and a hold JSON Web Key Set.
// It also holds the failure reason if parse failed.
//
// Usage example:
//   std::unique_ptr<Jwks> keys = Jwks::CreateFrom(jwks_string);
//   if(keys->GetStatus() == Status::OK) { ... }
//
class Jwks : public WithStatus {
public:
  // Format of public key.
  enum Type { PEM, JWKS };

  // Create from string.
  static std::unique_ptr<Jwks> CreateFrom(const std::string& pkey, Type type);

  // Struct for JSON Web Key
  struct Jwk {
    bssl::UniquePtr<EVP_PKEY> evp_pkey;
    bssl::UniquePtr<EC_KEY> ec_key;
    std::string kid;
    std::string kty;
    std::string alg;
    bool alg_specified = false;
    bool kid_specified = false;
    bool pem_format = false;
  };

  // Access to list of Jwks
  const std::vector<std::unique_ptr<Jwk>>& keys() const { return keys_; }

private:
  // Create Pem
  void CreateFromPemCore(const std::string& pkey_pem);
  // Create Jwks
  void CreateFromJwksCore(const std::string& pkey_jwks);

  // Extracts the public key from a jwk key (jkey) and sets it to keys_;
  void ExtractJwk(Json::ObjectSharedPtr jwk_json);
  // Create RSA Jwk
  void ExtractJwkFromJwkRSA(Json::ObjectSharedPtr jwk_json);
  // Create EC Jwk
  void ExtractJwkFromJwkEC(Json::ObjectSharedPtr jwk_json);

  // List of Jwks
  std::vector<std::unique_ptr<Jwk>> keys_;
};

} // namespace JwtAuthn
} // namespace Envoy
