#include "common/jwt_authn/jwt.h"

#include <algorithm>

#include "common/common/base64.h"
#include "common/common/utility.h"
#include "common/json/json_loader.h"

namespace Envoy {
namespace JwtAuthn {

Status Jwt::parseFromString(const std::string& jwt) {
  // jwt must have exactly 2 dots
  if (std::count(jwt.begin(), jwt.end(), '.') != 2) {
    return Status::JwtBadFormat;
  }
  auto jwt_split = StringUtil::splitToken(jwt, ".");
  if (jwt_split.size() != 3) {
    return Status::JwtBadFormat;
  }

  // Parse header json
  header_str_base64url_ = std::string(jwt_split[0].begin(), jwt_split[0].end());
  header_str_ = Base64Url::decode(header_str_base64url_);
  Json::ObjectSharedPtr header_json;
  try {
    header_json = Json::Factory::loadFromString(header_str_);
  } catch (Json::Exception& e) {
    return Status::JwtHeaderParseError;
  }

  // Header should contain "alg".
  if (!header_json->hasObject("alg")) {
    return Status::JwtHeaderNoAlg;
  }
  try {
    alg_ = header_json->getString("alg");
  } catch (Json::Exception& e) {
    return Status::JwtHeaderBadAlg;
  }

  if (alg_ != "RS256" && alg_ != "ES256") {
    return Status::JwtHeaderNotImplementedAlg;
  }

  // Header may contain "kid", which should be a string if exists.
  try {
    kid_ = header_json->getString("kid", "");
  } catch (Json::Exception& e) {
    return Status::JwtHeaderBadKid;
  }

  // Parse payload json
  payload_str_base64url_ = std::string(jwt_split[1].begin(), jwt_split[1].end());
  payload_str_ = Base64Url::decode(payload_str_base64url_);
  Json::ObjectSharedPtr payload_json;
  try {
    payload_json = Json::Factory::loadFromString(payload_str_);
  } catch (Json::Exception& e) {
    return Status::JwtPayloadParseError;
  }

  iss_ = payload_json->getString("iss", "");
  sub_ = payload_json->getString("sub", "");
  exp_ = payload_json->getInteger("exp", 0);

  // "aud" can be either string array or string.
  // Try as string array, read it as empty array if doesn't exist.
  try {
    audiences_ = payload_json->getStringArray("aud", true);
  } catch (Json::Exception& e) {
    // Try as string
    try {
      auto audience = payload_json->getString("aud");
      audiences_.push_back(audience);
    } catch (Json::Exception& e) {
      return Status::JwtPayloadParseError;
    }
  }

  // Set up signature
  signature_ = Base64Url::decode(std::string(jwt_split[2].begin(), jwt_split[2].end()));
  if (signature_ == "") {
    // Signature is a bad Base64url input.
    return Status::JwtSignatureParseError;
  }
  return Status::Ok;
}

} // namespace JwtAuthn
} // namespace Envoy
