#include "common/jwt_authn/jwt.h"

#include <algorithm>

#include "common/common/utility.h"
#include "common/json/json_loader.h"
#include "common/jwt_authn/utils.h"

namespace Envoy {
namespace JwtAuthn {

Status Jwt::ParseFromString(const std::string& jwt) {
  // jwt must have exactly 2 dots
  if (std::count(jwt.begin(), jwt.end(), '.') != 2) {
    return Status::JWT_BAD_FORMAT;
  }
  auto jwt_split = StringUtil::splitToken(jwt, ".");
  if (jwt_split.size() != 3) {
    return Status::JWT_BAD_FORMAT;
  }

  // Parse header json
  header_str_base64url = std::string(jwt_split[0].begin(), jwt_split[0].end());
  header_str = Base64UrlDecode(header_str_base64url);
  try {
    header_json = Json::Factory::loadFromString(header_str);
  } catch (Json::Exception& e) {
    return Status::JWT_HEADER_PARSE_ERROR;
  }

  // Header should contain "alg".
  if (!header_json->hasObject("alg")) {
    return Status::JWT_HEADER_NO_ALG;
  }
  try {
    alg = header_json->getString("alg");
  } catch (Json::Exception& e) {
    return Status::JWT_HEADER_BAD_ALG;
  }

  if (alg != "RS256" && alg != "ES256") {
    return Status::ALG_NOT_IMPLEMENTED;
  }

  // Header may contain "kid", which should be a string if exists.
  try {
    kid = header_json->getString("kid", "");
  } catch (Json::Exception& e) {
    return Status::JWT_HEADER_BAD_KID;
  }

  // Parse payload json
  payload_str_base64url = std::string(jwt_split[1].begin(), jwt_split[1].end());
  payload_str = Base64UrlDecode(payload_str_base64url);
  try {
    payload_json = Json::Factory::loadFromString(payload_str);
  } catch (Json::Exception& e) {
    return Status::JWT_PAYLOAD_PARSE_ERROR;
  }

  iss = payload_json->getString("iss", "");
  sub = payload_json->getString("sub", "");
  exp = payload_json->getInteger("exp", 0);

  // "aud" can be either string array or string.
  // Try as string array, read it as empty array if doesn't exist.
  try {
    aud = payload_json->getStringArray("aud", true);
  } catch (Json::Exception& e) {
    // Try as string
    try {
      auto audience = payload_json->getString("aud");
      aud.push_back(audience);
    } catch (Json::Exception& e) {
      return Status::JWT_PAYLOAD_PARSE_ERROR;
    }
  }

  // Set up signature
  signature = Base64UrlDecode(std::string(jwt_split[2].begin(), jwt_split[2].end()));
  if (signature == "") {
    // Signature is a bad Base64url input.
    return Status::JWT_SIGNATURE_PARSE_ERROR;
  }
  return Status::OK;
}

} // namespace JwtAuthn
} // namespace Envoy
