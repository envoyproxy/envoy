#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace InjectedCredentials {
namespace OAuth2 {
/**
 * Callback interface to enable the OAuth client to trigger actions upon completion of an
 * asynchronous HTTP request/response.
 */
class FilterCallbacks {
public:
  virtual ~FilterCallbacks() = default;
  enum class FailureReason {
    StreamReset,
    BadToken,
    BadResponseCode,
  };

  virtual void onGetAccessTokenSuccess(const std::string& access_token,
                                       std::chrono::seconds expires_in) PURE;
  virtual void onGetAccessTokenFailure(FailureReason) PURE;
};

} // namespace OAuth2
} // namespace InjectedCredentials
} // namespace Http
} // namespace Extensions
} // namespace Envoy
