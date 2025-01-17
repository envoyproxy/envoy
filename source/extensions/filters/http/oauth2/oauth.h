#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth2 {

/**
 * Callback interface to enable the OAuth client to trigger actions upon completion of an
 * asynchronous HTTP request/response.
 */
class FilterCallbacks {
public:
  virtual ~FilterCallbacks() = default;

  virtual void onGetAccessTokenSuccess(const std::string& access_token, const std::string& id_token,
                                       const std::string& refresh_token,
                                       std::chrono::seconds expires_in) PURE;

  virtual void onRefreshAccessTokenSuccess(const std::string& access_token,
                                           const std::string& id_token,
                                           const std::string& refresh_token,
                                           std::chrono::seconds expires_in) PURE;

  virtual void onRefreshAccessTokenFailure() PURE;

  virtual void sendUnauthorizedResponse() PURE;
};

/**
 * Describes the authentication type used by the client when communicating with the auth server.
 */
enum class AuthType { UrlEncodedBody, BasicAuth };

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
