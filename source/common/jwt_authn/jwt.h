#pragma once

#include "common/jwt_authn/status.h"

#include <string>
#include <vector>

namespace Envoy {
namespace JwtAuthn {

/**
 * struct to hold a JWT data.
 */
struct Jwt {
  // header string
  std::string header_str_;
  // header base64_url encoded
  std::string header_str_base64url_;

  // payload string
  std::string payload_str_;
  // payload base64_url encoded
  std::string payload_str_base64url_;
  // signature string
  std::string signature_;
  // alg
  std::string alg_;
  // kid
  std::string kid_;
  // iss
  std::string iss_;
  // audiences
  std::vector<std::string> audiences_;
  // sub
  std::string sub_;
  // expiration
  int64_t exp_ = 0;

  /**
   * Parse Jwt from string text
   * @return the status.
   */
  Status parseFromString(const std::string& jwt);
};

} // namespace JwtAuthn
} // namespace Envoy
