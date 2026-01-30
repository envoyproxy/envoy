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
// limitations under the License.

#pragma once

#include "source/common/jwt/jwks.h"
#include "source/common/jwt/jwt.h"
#include "source/common/jwt/status.h"

namespace Envoy {
namespace JwtVerify {

/**
 * This function verifies JWT signature is valid.
 * If verification failed, returns the failure reason.
 * Note this method does not verify the "aud" claim.
 * @param jwt is Jwt object
 * @param jwks is Jwks object
 * @return the verification status
 */
Status verifyJwtWithoutTimeChecking(const Jwt& jwt, const Jwks& jwks);

/**
 * This function verifies JWT signature is valid and that it has not expired
 * checking the "exp" and "nbf" claims against the system's current wall clock.
 * If verification failed, returns the failure reason.
 * Note this method does not verify the "aud" claim.
 * @param jwt is Jwt object
 * @param jwks is Jwks object
 * @return the verification status
 */
Status verifyJwt(const Jwt& jwt, const Jwks& jwks);

/**
 * This function verifies JWT signature is valid and that it has not expired
 * checking the "exp" and "nbf" claims against the provided time. If
 * verification failed, returns the failure reason. Note this method does not
 * verify the "aud" claim.
 * @param jwt is Jwt object
 * @param jwks is Jwks object
 * @param now is the number of seconds since the unix epoch
 * @param clock_skew is the clock skew in second
 * @return the verification status
 */
Status verifyJwt(const Jwt& jwt, const Jwks& jwks, uint64_t now,
                 uint64_t clock_skew = kClockSkewInSecond);

/**
 * This function verifies JWT signature is valid, that it has not expired
 * checking the "exp" and "nbf" claims against the system's current wall clock
 * as well as validating that one of the entries in the audience list appears
 * as a member in the "aud" claim of the specified JWT.  If the supplied
 * audience list is empty, no verification of the JWT's "aud" field is
 * performed. If verification failed, returns the failure reason.
 * @param jwt is Jwt object
 * @param jwks is Jwks object
 * @param audiences a list of audience by which to check against
 * @return the verification status
 */
Status verifyJwt(const Jwt& jwt, const Jwks& jwks,
                 const std::vector<std::string>& audiences);

/**
 * This function verifies JWT signature is valid, that it has not expired
 * checking the "exp" and "nbf" claims against the provided time
 * as well as validating that one of the entries in the audience list appears
 * as a member in the "aud" claim of the specified JWT. If the supplied
 * audience list is empty, no verification of the JWT's "aud" field is
 * performed.
 * If verification failed,
 * returns the failure reason.
 * @param jwt is Jwt object
 * @param jwks is Jwks object
 * @param audiences a list of audience by which to check against.
 * @return the verification status
 */
Status verifyJwt(const Jwt& jwt, const Jwks& jwks,
                 const std::vector<std::string>& audiences, uint64_t now);

}  // namespace JwtVerify
}  // namespace Envoy
