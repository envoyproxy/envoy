#include "source/extensions/filters/http/oauth2/oauth_client.h"

#include <chrono>

#include "envoy/http/async_client.h"
#include "envoy/http/message.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/base64.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/common/logger.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/oauth2/oauth_response.pb.h"

using namespace std::chrono_literals;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth2 {

namespace {
constexpr const char* UrlBodyTemplateWithCredentialsForAuthCode =
    "grant_type=authorization_code&code={0}&client_id={1}&client_secret={2}&redirect_uri={3}";

constexpr const char* UrlBodyTemplateWithoutCredentialsForAuthCode =
    "grant_type=authorization_code&code={0}&redirect_uri={1}";

constexpr const char* UrlBodyTemplateWithCredentialsForRefreshToken =
    "grant_type=refresh_token&refresh_token={0}&client_id={1}&client_secret={2}";

constexpr const char* UrlBodyTemplateWithoutCredentialsForRefreshToken =
    "grant_type=refresh_token&refresh_token={0}";

} // namespace

void OAuth2ClientImpl::asyncGetAccessToken(const std::string& auth_code,
                                           const std::string& client_id, const std::string& secret,
                                           const std::string& cb_url, AuthType auth_type) {
  ASSERT(state_ == OAuthState::Idle);
  state_ = OAuthState::PendingAccessToken;

  const auto encoded_cb_url = Http::Utility::PercentEncoding::encode(cb_url, ":/=&?");
  Http::RequestMessagePtr request = createPostRequest();
  std::string body;

  switch (auth_type) {
  case AuthType::UrlEncodedBody:
    body = fmt::format(UrlBodyTemplateWithCredentialsForAuthCode, auth_code,
                       Http::Utility::PercentEncoding::encode(client_id, ":/=&?"),
                       Http::Utility::PercentEncoding::encode(secret, ":/=&?"), encoded_cb_url);
    break;
  case AuthType::BasicAuth:
    const auto basic_auth_token = absl::StrCat(client_id, ":", secret);
    const auto encoded_token = Base64::encode(basic_auth_token.data(), basic_auth_token.size());
    const auto basic_auth_header_value = absl::StrCat("Basic ", encoded_token);
    request->headers().appendCopy(Http::CustomHeaders::get().Authorization,
                                  basic_auth_header_value);
    body = fmt::format(UrlBodyTemplateWithoutCredentialsForAuthCode, auth_code, encoded_cb_url);
    break;
  }

  request->body().add(body);
  request->headers().setContentLength(body.length());
  ENVOY_LOG(debug, "Dispatching OAuth request for access token.");
  dispatchRequest(std::move(request));
}

void OAuth2ClientImpl::asyncRefreshAccessToken(const std::string& refresh_token,
                                               const std::string& client_id,
                                               const std::string& secret, AuthType auth_type) {
  ASSERT(state_ == OAuthState::Idle);
  state_ = OAuthState::PendingAccessTokenByRefreshToken;

  Http::RequestMessagePtr request = createPostRequest();
  std::string body;

  switch (auth_type) {
  case AuthType::UrlEncodedBody:
    body = fmt::format(UrlBodyTemplateWithCredentialsForRefreshToken,
                       Http::Utility::PercentEncoding::encode(refresh_token, ":/=&?"),
                       Http::Utility::PercentEncoding::encode(client_id, ":/=&?"),
                       Http::Utility::PercentEncoding::encode(secret, ":/=&?"));
    break;
  case AuthType::BasicAuth:
    const auto basic_auth_token = absl::StrCat(client_id, ":", secret);
    const auto encoded_token = Base64::encode(basic_auth_token.data(), basic_auth_token.size());
    const auto basic_auth_header_value = absl::StrCat("Basic ", encoded_token);
    request->headers().appendCopy(Http::CustomHeaders::get().Authorization,
                                  basic_auth_header_value);
    body = fmt::format(UrlBodyTemplateWithoutCredentialsForRefreshToken,
                       Http::Utility::PercentEncoding::encode(refresh_token));
    break;
  }

  request->body().add(body);
  request->headers().setContentLength(body.length());
  ENVOY_LOG(debug, "Dispatching OAuth request for update access token by refresh token.");
  dispatchRequest(std::move(request));
}

void OAuth2ClientImpl::dispatchRequest(Http::RequestMessagePtr&& msg) {
  const auto thread_local_cluster = cm_.getThreadLocalCluster(uri_.cluster());
  if (thread_local_cluster != nullptr) {
    auto options = Http::AsyncClient::RequestOptions().setTimeout(
        std::chrono::milliseconds(PROTOBUF_GET_MS_REQUIRED(uri_, timeout)));

    if (retry_policy_.has_value()) {
      options.setRetryPolicy(retry_policy_.value());
      options.setBufferBodyForRetry(true);
    }

    in_flight_request_ =
        thread_local_cluster->httpAsyncClient().send(std::move(msg), *this, options);
  } else {
    parent_->sendUnauthorizedResponse();
  }
}

void OAuth2ClientImpl::onSuccess(const Http::AsyncClient::Request&,
                                 Http::ResponseMessagePtr&& message) {
  in_flight_request_ = nullptr;

  ASSERT(state_ == OAuthState::PendingAccessToken ||
         state_ == OAuthState::PendingAccessTokenByRefreshToken);
  const OAuthState oldState = state_;
  state_ = OAuthState::Idle;

  // Check that the auth cluster returned a happy response.
  const auto response_code = message->headers().Status()->value().getStringView();

  if (response_code != "200") {
    ENVOY_LOG(debug, "Oauth response code: {}", response_code);
    ENVOY_LOG(debug, "Oauth response body: {}", message->bodyAsString());
    switch (oldState) {
    case OAuthState::PendingAccessToken:
      parent_->sendUnauthorizedResponse();
      break;
    case OAuthState::PendingAccessTokenByRefreshToken:
      parent_->onRefreshAccessTokenFailure();
      break;
    default:
      PANIC("Malformed oauth client state");
    }
    return;
  }

  const std::string response_body = message->bodyAsString();

  envoy::extensions::http_filters::oauth2::OAuthResponse response;
  TRY_NEEDS_AUDIT {
    MessageUtil::loadFromJson(response_body, response, ProtobufMessage::getNullValidationVisitor());
  }
  END_TRY catch (EnvoyException& e) {
    ENVOY_LOG(debug, "Error parsing response body, received exception: {}", e.what());
    ENVOY_LOG(debug, "Response body: {}", response_body);
    parent_->sendUnauthorizedResponse();
    return;
  }

  // TODO(snowp): Should this be a pgv validation instead? A more readable log
  // message might be good enough reason to do this manually?
  if (!response.has_access_token()) {
    ENVOY_LOG(debug, "No access token after asyncGetAccessToken");
    parent_->sendUnauthorizedResponse();
    return;
  }

  const std::string access_token{PROTOBUF_GET_WRAPPED_REQUIRED(response, access_token)};
  const std::string id_token{PROTOBUF_GET_WRAPPED_OR_DEFAULT(response, id_token, EMPTY_STRING)};
  const std::string refresh_token{
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(response, refresh_token, EMPTY_STRING)};
  std::chrono::seconds expires_in = default_expires_in_;
  if (response.has_expires_in()) {
    expires_in = std::chrono::seconds{response.expires_in().value()};
  }
  if (expires_in <= 0s) {
    ENVOY_LOG(debug, "No default or explicit access token expiration after asyncGetAccessToken");
    parent_->sendUnauthorizedResponse();
    return;
  }

  switch (oldState) {
  case OAuthState::PendingAccessToken:
    parent_->onGetAccessTokenSuccess(access_token, id_token, refresh_token, expires_in);
    break;
  case OAuthState::PendingAccessTokenByRefreshToken:
    parent_->onRefreshAccessTokenSuccess(access_token, id_token, refresh_token, expires_in);
    break;
  default:
    PANIC("Malformed oauth client state");
  }
}

void OAuth2ClientImpl::onFailure(const Http::AsyncClient::Request&,
                                 Http::AsyncClient::FailureReason) {
  ENVOY_LOG(debug, "OAuth request failed.");
  in_flight_request_ = nullptr;
  const OAuthState oldState = state_;
  state_ = OAuthState::Idle;

  switch (oldState) {
  case OAuthState::PendingAccessToken:
    parent_->sendUnauthorizedResponse();
    break;
  case OAuthState::PendingAccessTokenByRefreshToken:
    parent_->onRefreshAccessTokenFailure();
    break;
  default:
    PANIC("Malformed oauth client state");
  }
}

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
