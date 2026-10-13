#pragma once
#include <chrono>
#include <memory>
#include <string>

#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "source/common/common/utility.h"
#include "source/common/jwt/jwt.h"
#include "source/common/jwt/verify.h"

using envoy::extensions::filters::http::jwt_authn::v3::JwtCacheConfig;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

// Cache key is the JWT string, value is parsed JWT struct.

class JwtCache;
using JwtCachePtr = std::unique_ptr<JwtCache>;

class JwtCache {
public:
  virtual ~JwtCache() = default;

  // Lookup a JWT in the cache, if found return the pointer to its parsed jwt struct.
  // If no found, return nullptr.
  virtual JwtVerify::Jwt* lookup(const std::string& token) PURE;

  // Insert a JWT and its parsed JWT struct to the cache.
  // The function will take over the ownership of jwt object.
  virtual void insert(const std::string& token, std::unique_ptr<JwtVerify::Jwt>&& jwt) PURE;

  // JwtCache factory function.
  static JwtCachePtr create(bool enable_cache, const JwtCacheConfig& config,
                            TimeSource& time_source);
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
