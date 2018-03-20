#include "common/jwt_authn/jwt.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/json/json_loader.h"
#include "openssl/bn.h"
#include "openssl/ecdsa.h"
#include "openssl/evp.h"
#include "openssl/rsa.h"
#include "openssl/sha.h"

#include <algorithm>
#include <cassert>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace Envoy {
namespace JwtAuthn {

Jwt::Jwt(const std::string &jwt) {
  // jwt must have exactly 2 dots
  if (std::count(jwt.begin(), jwt.end(), '.') != 2) {
    UpdateStatus(Status::JWT_BAD_FORMAT);
    return;
  }
  auto jwt_split = StringUtil::splitToken(jwt, ".");
  if (jwt_split.size() != 3) {
    UpdateStatus(Status::JWT_BAD_FORMAT);
    return;
  }

  // Parse header json
  header_str_base64url_ = std::string(jwt_split[0].begin(), jwt_split[0].end());
  header_str_ = Base64UrlDecode(header_str_base64url_);
  try {
    header_ = Json::Factory::loadFromString(header_str_);
  } catch (Json::Exception &e) {
    UpdateStatus(Status::JWT_HEADER_PARSE_ERROR);
    return;
  }

  // Header should contain "alg".
  if (!header_->hasObject("alg")) {
    UpdateStatus(Status::JWT_HEADER_NO_ALG);
    return;
  }
  try {
    alg_ = header_->getString("alg");
  } catch (Json::Exception &e) {
    UpdateStatus(Status::JWT_HEADER_BAD_ALG);
    return;
  }

  // Prepare EVP_MD object.
  if (alg_ == "RS256") {
    // may use
    // EVP_sha384() if alg == "RS384" and
    // EVP_sha512() if alg == "RS512"
    md_ = EVP_sha256();
  } else if (alg_ != "ES256") {
    UpdateStatus(Status::ALG_NOT_IMPLEMENTED);
    return;
  }

  // Header may contain "kid", which should be a string if exists.
  try {
    kid_ = header_->getString("kid", "");
  } catch (Json::Exception &e) {
    UpdateStatus(Status::JWT_HEADER_BAD_KID);
    return;
  }

  // Parse payload json
  payload_str_base64url_ =
      std::string(jwt_split[1].begin(), jwt_split[1].end());
  payload_str_ = Base64UrlDecode(payload_str_base64url_);
  try {
    payload_ = Json::Factory::loadFromString(payload_str_);
  } catch (Json::Exception &e) {
    UpdateStatus(Status::JWT_PAYLOAD_PARSE_ERROR);
    return;
  }

  iss_ = payload_->getString("iss", "");
  sub_ = payload_->getString("sub", "");
  exp_ = payload_->getInteger("exp", 0);

  // "aud" can be either string array or string.
  // Try as string array, read it as empty array if doesn't exist.
  try {
    aud_ = payload_->getStringArray("aud", true);
  } catch (Json::Exception &e) {
    // Try as string
    try {
      auto audience = payload_->getString("aud");
      aud_.push_back(audience);
    } catch (Json::Exception &e) {
      UpdateStatus(Status::JWT_PAYLOAD_PARSE_ERROR);
      return;
    }
  }

  // Set up signature
  signature_ =
      Base64UrlDecode(std::string(jwt_split[2].begin(), jwt_split[2].end()));
  if (signature_ == "") {
    // Signature is a bad Base64url input.
    UpdateStatus(Status::JWT_SIGNATURE_PARSE_ERROR);
    return;
  }
}

}  // namespace JwtAuthn
}  // namespace Envoy
