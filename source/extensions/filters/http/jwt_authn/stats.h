#pragma once

#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

/**
 * All stats for the Jwt Authn filter. @see stats_macros.h
 */
#define ALL_JWT_AUTHN_FILTER_STATS(COUNTER)                                                        \
  COUNTER(allowed)                                                                                 \
  COUNTER(cors_preflight_bypassed)                                                                 \
  COUNTER(denied)                                                                                  \
  COUNTER(jwks_fetch_success)                                                                      \
  COUNTER(jwks_fetch_failed)                                                                       \
  COUNTER(jwt_cache_hit)                                                                           \
  COUNTER(jwt_cache_miss)

/**
 * Wrapper struct for jwt_authn filter stats. @see stats_macros.h
 */
struct JwtAuthnFilterStats {
  ALL_JWT_AUTHN_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
