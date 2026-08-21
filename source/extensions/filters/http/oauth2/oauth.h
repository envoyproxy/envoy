#pragma once

#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/http/filter.h"
#include "envoy/stream_info/stream_info.h"

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

  virtual Http::FilterHeadersStatus onRefreshAccessTokenFailure() PURE;

  virtual Http::FilterHeadersStatus handleOAuthFailure(const std::string& reason,
                                                       const std::string& extra_details = "") PURE;
};

/**
 * Describes the authentication type used by the client when communicating with the auth server.
 */
enum class AuthType { UrlEncodedBody, BasicAuth, TlsClientAuth, PrivateKeyJwt };

/**
 * Returns the log tags attached to the OAuth2 application logs so that they can be correlated with
 * the access log on a per-request basis. RequestId matches the access log %STREAM_ID% /
 * x-request-id value.
 *
 * The tagged log macros only evaluate their tags argument when a log line is actually emitted, so
 * this is not called on every request.
 */
inline std::map<std::string, std::string>
oauthLogTags(Http::StreamDecoderFilterCallbacks& decoder_callbacks) {
  std::map<std::string, std::string> log_tags;
  const auto provider = decoder_callbacks.streamInfo().getStreamIdProvider();
  if (provider.has_value()) {
    const auto request_id = provider->toStringView();
    if (request_id.has_value()) {
      log_tags.emplace("RequestId", std::string(request_id.value()));
    }
  }
  return log_tags;
}

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
