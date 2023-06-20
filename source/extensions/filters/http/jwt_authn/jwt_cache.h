#pragma once
#include <chrono>
#include <memory>
#include <string>

#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "source/common/common/utility.h"

#include "jwt_verify_lib/jwt.h"
#include "jwt_verify_lib/verify.h"

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

  // Lookup a JWT token in the cache, if found return the pointer to its parsed jwt struct.
  // If no found, return nullptr.
  virtual ::google::jwt_verify::Jwt* lookup(const std::string& token) PURE;

  // Insert a JWT token and its parsed JWT struct to the cache.
  // The function will take over the ownership of jwt object.
  virtual void insert(const std::string& token,
                      std::unique_ptr<::google::jwt_verify::Jwt>&& jwt) PURE;

  // JwtCache factory function.
  static JwtCachePtr create(bool enable_cache, const JwtCacheConfig& config,
                            TimeSource& time_source);
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
