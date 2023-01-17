#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Credentials {

/**
 * Callback interface to enable the OAuth client to trigger actions upon completion of an
 * asynchronous HTTP request/response.
 */
class FilterCallbacks {
public:
  virtual ~FilterCallbacks() = default;

  virtual void onGetAccessTokenSuccess(const std::string& access_token,
                                       const std::string& refresh_token,
                                       std::chrono::seconds expires_in) PURE;

  virtual void sendUnauthorizedResponse() PURE;
};

} // namespace Credentials
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
