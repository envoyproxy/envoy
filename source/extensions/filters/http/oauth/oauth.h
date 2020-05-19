#pragma once

#include <string>
#include <vector>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth {

/**
 * Callback interface to enable the OAuth client to trigger actions upon completion of an
 * asynchronous HTTP request/response.
 */
class OAuth2FilterCallbacks {
public:
  OAuth2FilterCallbacks() = default;
  virtual ~OAuth2FilterCallbacks() = default;

  virtual void onGetAccessTokenSuccess(const std::string& access_token,
                                       const std::string& expires_in) PURE;

  virtual void onGetIdentitySuccess(const std::string& username) PURE;
  virtual void sendUnauthorizedResponse() PURE;
};

} // namespace Oauth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
