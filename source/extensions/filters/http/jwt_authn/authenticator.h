#pragma once

#include "envoy/server/filter_config.h"

#include "extensions/filters/http/jwt_authn/extractor.h"
#include "extensions/filters/http/jwt_authn/jwks_cache.h"

#include "jwt_verify_lib/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

class Authenticator;
typedef std::unique_ptr<Authenticator> AuthenticatorPtr;

typedef std::function<void(const ::google::jwt_verify::Status& status)> AuthenticatorCallback;

/**
 *  Authenticator object to handle all JWT authentication flow.
 */

class Authenticator {
public:
  virtual ~Authenticator() {}

  // Verify if headers satisfyies the JWT requirements. Can be limited to single provider with
  // extract_param.
  virtual void verify(Http::HeaderMap& headers, std::vector<JwtLocationConstPtr>&& tokens,
                      AuthenticatorCallback callback) PURE;

  // Called when the object is about to be destroyed.
  virtual void onDestroy() PURE;

  // Authenticator factory function.
  static AuthenticatorPtr create(const AudienceCheckerSupplier& audience_checker_suppiler,
                                 const absl::optional<std::string>& provider, JwksCache& jwks_cache,
                                 Upstream::ClusterManager& cluster_manager);
};

/**
 * Interface for authenticator factory.
 */
class AuthFactory {
public:
  virtual ~AuthFactory() {}

  // Factory method for creating authenticator, and populate it with provider config.
  virtual AuthenticatorPtr create(const AudienceCheckerSupplier* audience_checker_suppiler,
                                  const absl::optional<std::string>& provider) const PURE;
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
