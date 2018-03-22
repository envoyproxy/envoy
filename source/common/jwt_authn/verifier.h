#pragma once

#include "common/jwt_authn/jwks.h"
#include "common/jwt_authn/jwt.h"
#include "common/jwt_authn/status.h"

namespace Envoy {
namespace JwtAuthn {

/**
 * This function verifies JWT signature.
 * If verification failed, returns the failture reason.
 * @param jwt is Jwt object
 * @param jwks is Jwks object
 * @return the verification status
 */
Status verifyJwt(const Jwt& jwt, const Jwks& jwks);

} // namespace JwtAuthn
} // namespace Envoy
