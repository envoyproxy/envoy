#pragma once

#include "envoy/json/json_object.h"
#include "openssl/ec.h"
#include "openssl/evp.h"

#include <string>
#include <utility>
#include <vector>

namespace Envoy {
namespace JwtAuthn {

// JWT Verifier class.
//
// Usage example:
//   Verifier v;
//   Jwt jwt(jwt_string);
//   std::unique_ptr<Jwkss> pubkey = ...
//   if (v.Verify(jwt, *pubkey)) {
//     auto payload = jwt.Payload();
//     ...
//   } else {
//     Status s = v.GetStatus();
//     ...
//   }
class Verifier : public WithStatus {
 public:
  // This function verifies JWT signature.
  // If verification failed, GetStatus() returns the failture reason.
  // When the given JWT has a format error, this verification always fails and
  // the JWT's status is handed over to Verifier.
  // When pubkeys.GetStatus() is not equal to Status::OK, this verification
  // always fails and the public key's status is handed over to Verifier.
  bool Verify(const Jwt& jwt, const Jwkss& pubkeys);

 private:
  // Functions to verify with single public key.
  // (Note: Jwkss object passed to Verify() may contains multiple public keys)
  // When verification fails, UpdateStatus() is NOT called.
  bool VerifySignatureRSA(EVP_PKEY* key, const EVP_MD* md,
                          const uint8_t* signature, size_t signature_len,
                          const uint8_t* signed_data, size_t signed_data_len);
  bool VerifySignatureRSA(EVP_PKEY* key, const EVP_MD* md,
                          const std::string& signature,
                          const std::string& signed_data);
  bool VerifySignatureEC(EC_KEY* key, const std::string& signature,
                         const std::string& signed_data);
  bool VerifySignatureEC(EC_KEY* key, const uint8_t* signature,
                         size_t signature_len, const uint8_t* signed_data,
                         size_t signed_data_len);
};

}  // namespace JwtAuthn
}  // namespace Envoy
