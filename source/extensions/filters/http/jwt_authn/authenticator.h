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
  virtual void verify(Http::HeaderMap& headers, Callbacks* callback) PURE;

  // Called when the object is about to be destroyed.
  virtual void onDestroy() PURE;

  // Remove headers that configured to send JWT payloads
  virtual void sanitizePayloadHeaders(Http::HeaderMap& headers) const PURE;

  // Authenticator factory function.
  static AuthenticatorPtr create(FilterConfigSharedPtr config);
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
