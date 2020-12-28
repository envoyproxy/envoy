#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"

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

  virtual void onGetAccessTokenSuccess(const std::string& access_token,
                                       std::chrono::seconds expires_in, const std::string& subject,
                                       const std::vector<std::string>& groups) PURE;

  virtual void sendUnauthorizedResponse(const absl::string_view& message) PURE;
};

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
