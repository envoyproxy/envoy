#pragma once

#include "extensions/filters/http/jwt_authn/filter_config.h"

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

  // The callback interface to notify the completion event.
  class Callbacks {
  public:
    virtual ~Callbacks() {}
    virtual void onComplete(const ::google::jwt_verify::Status& status) PURE;
  };
  // Verify if headers satisfyies the JWT requirements. Can be limited to single provider with
  // extract_param.
  virtual void verify(const ExtractParam* extract_param, const absl::optional<std::string>& issuer,
                      Http::HeaderMap& headers, Callbacks* callback) PURE;

  // Called when the object is about to be destroyed.
  virtual void onDestroy() PURE;

  // Remove headers that configured to send JWT payloads
  virtual void sanitizePayloadHeaders(Http::HeaderMap& headers) const PURE;

  // Authenticator factory function.
  static AuthenticatorPtr create(FilterConfigSharedPtr config,
                                 const std::vector<std::string>& audiences = {});
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
