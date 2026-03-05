#pragma once

// Copyright 2018 Google LLC
// Copyright Envoy Project Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "source/common/jwt/status.h"

namespace Envoy {
namespace JwtVerify {

/**
 * RFC for JWT `aud <https://tools.ietf.org/html/rfc7519#section-4.1.3>`_ only
 * specifies case sensitive comparison. But experiences showed that users
 * easily add wrong scheme and trailing slash to cause mismatch.
 * In this implementation, scheme portion of URI and trailing slash is removed
 * before comparison.
 */
class CheckAudience {
public:
  // Construct the object with a list audiences from config.
  CheckAudience(const std::vector<std::string>& config_audiences);

  // Check any of jwt_audiences is matched with one of configured ones.
  bool areAudiencesAllowed(const std::vector<std::string>& jwt_audiences) const;

  // check if config audiences is empty
  bool empty() const { return config_audiences_.empty(); }

private:
  // configured audiences;
  std::set<std::string> config_audiences_;
};

typedef std::unique_ptr<CheckAudience> CheckAudiencePtr;

} // namespace JwtVerify
} // namespace Envoy
