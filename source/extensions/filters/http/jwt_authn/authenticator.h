#pragma once

#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/jwt_authn/extractor.h"
#include "source/extensions/filters/http/jwt_authn/jwks_cache.h"
#include "source/extensions/filters/http/jwt_authn/jwt_cache.h"

#include "jwt_verify_lib/check_audience.h"
#include "jwt_verify_lib/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

class Authenticator;
using AuthenticatorPtr = std::unique_ptr<Authenticator>;

using AuthenticatorCallback = std::function<void(const ::google::jwt_verify::Status& status)>;

using SetExtractedJwtDataCallback =
    std::function<void(const std::string&, const ProtobufWkt::Struct&)>;

using ClearRouteCacheCallback = std::function<void()>;

/**
 *  Authenticator object to handle all JWT authentication flow.
 */
class Authenticator {
public:
  virtual ~Authenticator() = default;

  // Verify if headers satisfies the JWT requirements. Can be limited to single provider with
  // extract_param.
  virtual void verify(Http::RequestHeaderMap& headers, Tracing::Span& parent_span,
                      std::vector<JwtLocationConstPtr>&& tokens,
                      SetExtractedJwtDataCallback set_extracted_jwt_data_cb,
                      AuthenticatorCallback callback, ClearRouteCacheCallback clear_route_cb) PURE;

  // Called when the object is about to be destroyed.
  virtual void onDestroy() PURE;

  // Authenticator factory function.
  static AuthenticatorPtr create(const ::google::jwt_verify::CheckAudience* check_audience,
                                 const absl::optional<std::string>& provider, bool allow_failed,
                                 bool allow_missing, JwksCache& jwks_cache,
                                 Upstream::ClusterManager& cluster_manager,
                                 CreateJwksFetcherCb create_jwks_fetcher_cb,
                                 TimeSource& time_source);
};

/**
 * Interface for authenticator factory.
 */
class AuthFactory {
public:
  virtual ~AuthFactory() = default;

  // Factory method for creating authenticator, and populate it with provider config.
  virtual AuthenticatorPtr create(const ::google::jwt_verify::CheckAudience* check_audience,
                                  const absl::optional<std::string>& provider, bool allow_failed,
                                  bool allow_missing) const PURE;
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
