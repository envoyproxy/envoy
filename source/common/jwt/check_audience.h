// Copyright 2018 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.#pragma once

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
 * easily add wrong scheme and tailing slash to cause mis-match.
 * In this implemeation, scheme portion of URI and tailing slash is removed
 * before comparison.
 */
class CheckAudience {
 public:
  // Construct the object with a list audiences from config.
  CheckAudience(const std::vector<std::string>& config_audiences);

  // Check any of jwt_audiences is matched with one of configurated ones.
  bool areAudiencesAllowed(const std::vector<std::string>& jwt_audiences) const;

  // check if config audiences is empty
  bool empty() const { return config_audiences_.empty(); }

 private:
  // configured audiences;
  std::set<std::string> config_audiences_;
};

typedef std::unique_ptr<CheckAudience> CheckAudiencePtr;

}  // namespace JwtVerify
}  // namespace Envoy
