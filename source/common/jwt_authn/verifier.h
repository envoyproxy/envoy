#pragma once

#include "common/jwt_authn/jwks.h"
#include "common/jwt_authn/jwt.h"
#include "common/jwt_authn/status.h"

namespace Envoy {
namespace JwtAuthn {

// This function verifies JWT signature.
// If verification failed, returns the failture reason.
Status VerifyJwt(const Jwt& jwt, const Jwks& jwks);

} // namespace JwtAuthn
} // namespace Envoy
