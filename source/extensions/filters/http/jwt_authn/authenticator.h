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

/**
 *  Authenticator object to handle all JWT authentication flow.
 */

class Authenticator {
public:
  virtual ~Authenticator() {}

  // Verify if headers satisfyies the JWT requirements. Can be limited to single provider with
  // extract_param.
  virtual void
  verify(Http::HeaderMap& headers, std::vector<JwtLocationConstPtr>&& tokens,
         std::function<void(const ::google::jwt_verify::Status& status)>&& callback) PURE;

  // Called when the object is about to be destroyed.
  virtual void onDestroy() PURE;

  // Remove headers that configured to send JWT payloads
  virtual void sanitizePayloadHeaders(Http::HeaderMap& headers) const PURE;

  // Authenticator factory function.
  static AuthenticatorPtr
  create(const std::vector<std::string>& audiences, const absl::optional<std::string>& issuer,
         bool allow_failed,
         const Protobuf::Map<ProtobufTypes::String,
                             ::envoy::config::filter::http::jwt_authn::v2alpha::JwtProvider>&
             providers,
         JwksCache& jwks_cache, Upstream::ClusterManager& cluster_manager);
};

/**
 * Interface for authenticator factory.
 */
class AuthFactory {
public:
  virtual ~AuthFactory() {}

  // Factory method for creating authenticator, and populate it with provider config.
  virtual AuthenticatorPtr create(const std::vector<std::string>& audiences,
                                  const absl::optional<std::string>& issuer,
                                  bool allow_failed) const PURE;
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
