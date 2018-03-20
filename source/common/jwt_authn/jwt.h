#pragma once

#include "envoy/json/json_object.h"
#include "openssl/ec.h"
#include "openssl/evp.h"

#include <string>
#include <utility>
#include <vector>

namespace Envoy {
namespace JwtAuthn {

// Class to parse and a hold a JWT.
// It also holds the failure reason if parse failed.
//
// Usage example:
//   Jwt jwt(jwt_string);
//   if(jwt.GetStatus() == Status::OK) { ... }
class Jwt : public WithStatus {
 public:
  // This constructor parses the given JWT and prepares for verification.
  // You can check if the setup was successfully done by seeing if GetStatus()
  // == Status::OK. When the given JWT has a format error, GetStatus() returns
  // the error detail.
  Jwt(const std::string& jwt);

  // It returns a pointer to a JSON object of the header of the given JWT.
  // When the given JWT has a format error, it returns nullptr.
  // It returns the header JSON even if the signature is invalid.
  Json::ObjectSharedPtr Header()  { return header_; }

  // It returns a pointer to a JSON object of the payload of the given JWT.
  // When the given jWT has a format error, it returns nullptr.
  // It returns the payload JSON even if the signature is invalid.
  Json::ObjectSharedPtr Payload() { return payload_; }

  // They return a string (or base64url-encoded string) of the header JSON of
  // the given JWT.
  const std::string& HeaderStr() const { return header_str_; }
  const std::string& HeaderStrBase64Url() const { return header_str_base64url_; }

  // They return the "alg" (or "kid") value of the header of the given JWT.
  const std::string& Alg() const { return alg_; }

  // It returns the "kid" value of the header of the given JWT, or an empty
  // string if "kid" does not exist in the header.
  const std::string& Kid() const { return kid_; }

  // They return a string (or base64url-encoded string) of the payload JSON of
  // the given JWT.
  const std::string& PayloadStr() const { return payload_str_; }
  const std::string& PayloadStrBase64Url() const { return payload_str_base64url_; }

  // It returns the "iss" claim value of the given JWT, or an empty string if
  // "iss" claim does not exist.
  const std::string& Iss() const { return iss_; }

  // It returns the "aud" claim value of the given JWT, or an empty string if
  // "aud" claim does not exist.
  const std::vector<std::string>& Aud() const { return aud_; }

  // It returns the "sub" claim value of the given JWT, or an empty string if
  // "sub" claim does not exist.
  const std::string& Sub() const { return sub_; }

  // It returns the "exp" claim value of the given JWT, or 0 if "exp" claim does
  // not exist.
  int64_t Exp() const { return exp_; }

 private:
  const EVP_MD* md_;

  Json::ObjectSharedPtr header_;
  std::string header_str_;
  std::string header_str_base64url_;
  Json::ObjectSharedPtr payload_;
  std::string payload_str_;
  std::string payload_str_base64url_;
  std::string signature_;
  std::string alg_;
  std::string kid_;
  std::string iss_;
  std::vector<std::string> aud_;
  std::string sub_;
  int64_t exp_;
};

}  // namespace JwtAuthn
}  // namespace Envoy
