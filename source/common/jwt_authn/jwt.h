#pragma once

#include "common/jwt_authn/status.h"
#include "envoy/json/json_object.h"

namespace Envoy {
namespace JwtAuthn {

// struct to hold a JWT data.
struct Jwt {
  // JSON object of the header
  Json::ObjectSharedPtr header_json;
  // header string
  std::string header_str;
  // header base64_url encoded
  std::string header_str_base64url;

  // SON object of the payload
  Json::ObjectSharedPtr payload_json;
  // payload string
  std::string payload_str;
  // payload base64_url encoded
  std::string payload_str_base64url;
  // signature string
  std::string signature;
  // alg
  std::string alg;
  // kid
  std::string kid;
  // iss
  std::string iss;
  // audiences
  std::vector<std::string> aud;
  // sub
  std::string sub;
  // expiration
  int64_t exp = 0;

  // Parse from string 
  Status ParseFromString(const std::string& jwt);
};

 
} // namespace JwtAuthn
} // namespace Envoy
