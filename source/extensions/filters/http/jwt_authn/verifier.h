#pragma once

#include "extensions/filters/http/jwt_authn/jwks.h"
#include "extensions/filters/http/jwt_authn/jwt.h"
#include "extensions/filters/http/jwt_authn/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
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
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
