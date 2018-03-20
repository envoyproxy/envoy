#pragma once

#include "common/jwt_authn/status.h"

#include "envoy/json/json_object.h"
#include "openssl/ec.h"
#include "openssl/evp.h"

#include <string>
#include <utility>
#include <vector>

namespace Envoy {
namespace JwtAuthn {

// Class to parse and a hold JSON Web Key Set.
// It also holds the failure reason if parse failed.
//
// Usage example:
//   std::unique_ptr<Jwks> keys = Jwks::ParseFromJwks(jwks_string);
//   if(keys->GetStatus() == Status::OK) { ... }
//
class Jwks : public WithStatus {
 public:
  // Format of public key.
  enum Type { PEM, JWKS };

  static std::unique_ptr<Jwks> CreateFrom(const std::string& pkey,
                                             Type type);

  class Jwk {
   public:
    Jwk(){};
    bssl::UniquePtr<EVP_PKEY> evp_pkey_;
    bssl::UniquePtr<EC_KEY> ec_key_;
    std::string kid_;
    std::string kty_;
    bool alg_specified_ = false;
    bool kid_specified_ = false;
    bool pem_format_ = false;
    std::string alg_;
  };

  const std::vector<std::unique_ptr<Jwk>>& keys() const { return keys_; }

 private:
  void CreateFromPemCore(const std::string& pkey_pem);
  void CreateFromJwksCore(const std::string& pkey_jwks);
  
  // Extracts the public key from a jwk key (jkey) and sets it to keys_;
  void ExtractJwk(Json::ObjectSharedPtr jwk_json);
  void ExtractJwkFromJwkRSA(Json::ObjectSharedPtr jwk_json);
  void ExtractJwkFromJwkEC(Json::ObjectSharedPtr jwk_json);

  std::vector<std::unique_ptr<Jwk> > keys_;
};

}  // namespace JwtAuthn
}  // namespace Envoy
