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
#include "source/common/json/json_loader.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/oauth2/oauth_response.pb.h"

using namespace std::chrono_literals;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth2 {

namespace {
constexpr absl::string_view WwwFormUrlEncodedReservedCharacters = ":/=&?+";
// Upper bound on a PAR request_uri from the authorization server before it is used in a redirect.
constexpr size_t kMaxRequestUriLength = 1024;

constexpr const char* UrlBodyTemplateWithCredentialsForAuthCode =
    "grant_type=authorization_code&code={0}&client_id={1}&client_secret={2}&redirect_uri={3}&code_"
    "verifier={4}";

constexpr const char* UrlBodyTemplateWithoutCredentialsForAuthCode =
    "grant_type=authorization_code&code={0}&redirect_uri={1}&code_verifier={2}";

constexpr const char* UrlBodyTemplateWithCredentialsForRefreshToken =
    "grant_type=refresh_token&refresh_token={0}&client_id={1}&client_secret={2}";

constexpr const char* UrlBodyTemplateWithoutCredentialsForRefreshToken =
    "grant_type=refresh_token&refresh_token={0}";

constexpr const char* UrlBodyTemplateWithoutSecretForAuthCode =
    "grant_type=authorization_code&code={0}&client_id={1}&redirect_uri={2}&code_verifier={3}";

constexpr const char* UrlBodyTemplateWithoutSecretForRefreshToken =
    "grant_type=refresh_token&refresh_token={0}&client_id={1}";

constexpr const char* UrlBodyTemplateWithAssertionForAuthCode =
    "grant_type=authorization_code&code={0}&client_id={1}"
    "&client_assertion_type=urn%3Aietf%3Aparams%3Aoauth%3Aclient-assertion-type%3Ajwt-bearer"
    "&client_assertion={2}&redirect_uri={3}&code_verifier={4}";

constexpr const char* UrlBodyTemplateWithAssertionForRefreshToken =
    "grant_type=refresh_token&refresh_token={0}&client_id={1}"
    "&client_assertion_type=urn%3Aietf%3Aparams%3Aoauth%3Aclient-assertion-type%3Ajwt-bearer"
    "&client_assertion={2}";

} // namespace

void OAuth2ClientImpl::asyncGetAccessToken(const std::string& auth_code,
                                           const std::string& client_id, const std::string& secret,
                                           const std::string& cb_url,
                                           const std::string& code_verifier, AuthType auth_type) {
  ASSERT(state_ == OAuthState::Idle);
  state_ = OAuthState::PendingAccessToken;

  const auto encoded_cb_url =
      Http::Utility::PercentEncoding::encode(cb_url, WwwFormUrlEncodedReservedCharacters);
  Http::RequestMessagePtr request = createPostRequest(uri_);
  std::string body;

  switch (auth_type) {
  case AuthType::UrlEncodedBody:
    body = fmt::format(
        UrlBodyTemplateWithCredentialsForAuthCode, auth_code,
        Http::Utility::PercentEncoding::encode(client_id, WwwFormUrlEncodedReservedCharacters),
        Http::Utility::PercentEncoding::encode(secret, WwwFormUrlEncodedReservedCharacters),
        encoded_cb_url, code_verifier);
    break;
  case AuthType::BasicAuth: {
    const auto basic_auth_token = absl::StrCat(client_id, ":", secret);
    const auto encoded_token = Base64::encode(basic_auth_token.data(), basic_auth_token.size());
    const auto basic_auth_header_value = absl::StrCat("Basic ", encoded_token);
    request->headers().appendCopy(Http::CustomHeaders::get().Authorization,
                                  basic_auth_header_value);
    body = fmt::format(UrlBodyTemplateWithoutCredentialsForAuthCode, auth_code, encoded_cb_url,
                       code_verifier);
    break;
  }
  case AuthType::TlsClientAuth:
    // For mTLS, authentication is done via the client certificate in the TLS handshake.
    // No client_secret is sent in the request body or headers.
    body = fmt::format(
        UrlBodyTemplateWithoutSecretForAuthCode, auth_code,
        Http::Utility::PercentEncoding::encode(client_id, WwwFormUrlEncodedReservedCharacters),
        encoded_cb_url, code_verifier);
    break;
  case AuthType::PrivateKeyJwt:
    // For private_key_jwt, the secret parameter contains the pre-built JWT assertion.
    body = fmt::format(
        UrlBodyTemplateWithAssertionForAuthCode, auth_code,
        Http::Utility::PercentEncoding::encode(client_id, WwwFormUrlEncodedReservedCharacters),
        Http::Utility::PercentEncoding::encode(secret, WwwFormUrlEncodedReservedCharacters),
        encoded_cb_url, code_verifier);
    break;
  }

  request->body().add(body);
  request->headers().setContentLength(body.length());
  ENVOY_TAGGED_STREAM_LOG(debug, oauthLogTags(*decoder_callbacks_), *decoder_callbacks_,
                          "Dispatching OAuth request for access token.");
  dispatchRequest(std::move(request), uri_);
}

void OAuth2ClientImpl::asyncRefreshAccessToken(const std::string& refresh_token,
                                               const std::string& client_id,
                                               const std::string& secret, AuthType auth_type) {
  ASSERT(state_ == OAuthState::Idle);
  state_ = OAuthState::PendingAccessTokenByRefreshToken;

  Http::RequestMessagePtr request = createPostRequest(uri_);
  std::string body;

  switch (auth_type) {
  case AuthType::UrlEncodedBody:
    body = fmt::format(
        UrlBodyTemplateWithCredentialsForRefreshToken,
        Http::Utility::PercentEncoding::encode(refresh_token, WwwFormUrlEncodedReservedCharacters),
        Http::Utility::PercentEncoding::encode(client_id, WwwFormUrlEncodedReservedCharacters),
        Http::Utility::PercentEncoding::encode(secret, WwwFormUrlEncodedReservedCharacters));
    break;
  case AuthType::BasicAuth: {
    const auto basic_auth_token = absl::StrCat(client_id, ":", secret);
    const auto encoded_token = Base64::encode(basic_auth_token.data(), basic_auth_token.size());
    const auto basic_auth_header_value = absl::StrCat("Basic ", encoded_token);
    request->headers().appendCopy(Http::CustomHeaders::get().Authorization,
                                  basic_auth_header_value);
    body = fmt::format(
        UrlBodyTemplateWithoutCredentialsForRefreshToken,
        Http::Utility::PercentEncoding::encode(refresh_token, WwwFormUrlEncodedReservedCharacters));
    break;
  }
  case AuthType::TlsClientAuth:
    // For mTLS, authentication is done via the client certificate in the TLS handshake.
    // No client_secret is sent in the request body or headers.
    body = fmt::format(
        UrlBodyTemplateWithoutSecretForRefreshToken,
        Http::Utility::PercentEncoding::encode(refresh_token, WwwFormUrlEncodedReservedCharacters),
        Http::Utility::PercentEncoding::encode(client_id, WwwFormUrlEncodedReservedCharacters));
    break;
  case AuthType::PrivateKeyJwt:
    // For private_key_jwt, the secret parameter contains the pre-built JWT assertion.
    body = fmt::format(
        UrlBodyTemplateWithAssertionForRefreshToken,
        Http::Utility::PercentEncoding::encode(refresh_token, WwwFormUrlEncodedReservedCharacters),
        Http::Utility::PercentEncoding::encode(client_id, WwwFormUrlEncodedReservedCharacters),
        Http::Utility::PercentEncoding::encode(secret, WwwFormUrlEncodedReservedCharacters));
    break;
  }

  request->body().add(body);
  request->headers().setContentLength(body.length());
  ENVOY_TAGGED_STREAM_LOG(debug, oauthLogTags(*decoder_callbacks_), *decoder_callbacks_,
                          "Dispatching OAuth request for update access token by refresh token.");
  dispatchRequest(std::move(request), uri_);
}

void OAuth2ClientImpl::cancel() {
  parent_ = nullptr;
  decoder_callbacks_ = nullptr;
  Http::AsyncClient::Request* in_flight = in_flight_request_;
  in_flight_request_ = nullptr;
  if (in_flight != nullptr) {
    in_flight->cancel();
  }
  state_ = OAuthState::Idle;
}

namespace {
// Percent-encodes each key and value, then joins them into a form body.
std::string encodeFormParams(const OAuth2Client::FormParams& params) {
  std::string body;
  for (const auto& [key, value] : params) {
    if (!body.empty()) {
      body += "&";
    }
    body += Http::Utility::PercentEncoding::encode(key, WwwFormUrlEncodedReservedCharacters);
    body += "=";
    body += Http::Utility::PercentEncoding::encode(value, WwwFormUrlEncodedReservedCharacters);
  }
  return body;
}
} // namespace

void OAuth2ClientImpl::asyncPushAuthorizationRequest(const FormParams& params,
                                                     const std::string& client_id,
                                                     const std::string& secret,
                                                     AuthType auth_type) {
  ASSERT(state_ == OAuthState::Idle);
  if (par_endpoint_ == nullptr) {
    parent_->onPushAuthorizationRequestFailure();
    return;
  }
  state_ = OAuthState::PendingPushedAuthorizationRequest;

  const std::string auth_params = encodeFormParams(params);
  Http::RequestMessagePtr request = createPostRequest(*par_endpoint_);
  std::string body;

  switch (auth_type) {
  case AuthType::UrlEncodedBody:
    body = fmt::format(
        "{}&client_id={}&client_secret={}", auth_params,
        Http::Utility::PercentEncoding::encode(client_id, WwwFormUrlEncodedReservedCharacters),
        Http::Utility::PercentEncoding::encode(secret, WwwFormUrlEncodedReservedCharacters));
    break;
  case AuthType::BasicAuth: {
    const auto basic_auth_token = absl::StrCat(client_id, ":", secret);
    const auto encoded_token = Base64::encode(basic_auth_token.data(), basic_auth_token.size());
    request->headers().appendCopy(Http::CustomHeaders::get().Authorization,
                                  absl::StrCat("Basic ", encoded_token));
    body = auth_params;
    break;
  }
  case AuthType::TlsClientAuth:
    body = fmt::format(
        "{}&client_id={}", auth_params,
        Http::Utility::PercentEncoding::encode(client_id, WwwFormUrlEncodedReservedCharacters));
    break;
  case AuthType::PrivateKeyJwt:
    // For private_key_jwt, the secret parameter contains the pre-built JWT assertion.
    body = fmt::format(
        "{}&client_id={}&client_assertion_type=urn%3Aietf%3Aparams%3Aoauth%3Aclient-assertion-type%3Ajwt-bearer&client_assertion={}",
        auth_params,
        Http::Utility::PercentEncoding::encode(client_id, WwwFormUrlEncodedReservedCharacters),
        Http::Utility::PercentEncoding::encode(secret, WwwFormUrlEncodedReservedCharacters));
    break;
  }

  request->body().add(body);
  request->headers().setContentLength(body.length());
  ENVOY_TAGGED_STREAM_LOG(debug, oauthLogTags(*decoder_callbacks_), *decoder_callbacks_,
                          "Dispatching OAuth pushed authorization request.");
  dispatchRequest(std::move(request), *par_endpoint_);
}

bool OAuth2ClientImpl::parsePushedAuthorizationResponse(const std::string& response_body) {
  request_uri_.clear();
  auto json_or_error = Json::Factory::loadFromString(response_body);
  if (!json_or_error.ok()) {
    return false;
  }
  auto request_uri_or_error = json_or_error.value()->getString("request_uri");
  if (!request_uri_or_error.ok()) {
    return false;
  }
  const std::string& request_uri = request_uri_or_error.value();
  if (request_uri.empty() || request_uri.size() > kMaxRequestUriLength) {
    return false;
  }
  for (const char c : request_uri) {
    if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) == 0x7f) {
      return false;
    }
  }
  request_uri_ = request_uri;
  return true;
}

void OAuth2ClientImpl::dispatchRequest(Http::RequestMessagePtr&& msg, const HttpUri& uri) {
  const auto thread_local_cluster = cm_.getThreadLocalCluster(uri.cluster());
  if (thread_local_cluster != nullptr) {
    auto options = Http::AsyncClient::RequestOptions().setTimeout(
        std::chrono::milliseconds(PROTOBUF_GET_MS_REQUIRED(uri, timeout)));

    // PAR POSTs are not retried: a replayed single-use client assertion or a retried POST can
    // leave orphan request_uri values.
    if (retry_policy_ != nullptr && state_ != OAuthState::PendingPushedAuthorizationRequest) {
      options.setRetryPolicy(retry_policy_);
      options.setBufferBodyForRetry(true);
    }

    in_flight_request_ =
        thread_local_cluster->httpAsyncClient().send(std::move(msg), *this, options);
  } else if (state_ == OAuthState::PendingPushedAuthorizationRequest) {
    state_ = OAuthState::Idle;
    parent_->onPushAuthorizationRequestFailure();
  } else {
    handleOAuthFailure(false, "Token endpoint cluster not found");
  }
}

void OAuth2ClientImpl::handleOAuthFailure(bool is_request_dispatched, const std::string& reason,
                                          const std::string& extra_details) {
  const auto result = parent_->handleOAuthFailure(reason, extra_details);
  if (is_request_dispatched) {
    if (result == Http::FilterHeadersStatus::Continue) {
      decoder_callbacks_->continueDecoding();
    }
  } else {
    state_ = (result == Http::FilterHeadersStatus::Continue) ? OAuthState::FailureContinue
                                                             : OAuthState::FailureStop;
  }
}

void OAuth2ClientImpl::handleRefreshTokenFailure(bool is_request_dispatched) {
  const auto result = parent_->onRefreshAccessTokenFailure();
  if (is_request_dispatched) {
    if (result == Http::FilterHeadersStatus::Continue) {
      decoder_callbacks_->continueDecoding();
    }
  } else {
    state_ = (result == Http::FilterHeadersStatus::Continue) ? OAuthState::FailureContinue
                                                             : OAuthState::FailureStop;
  }
}

void OAuth2ClientImpl::onSuccess(const Http::AsyncClient::Request&,
                                 Http::ResponseMessagePtr&& message) {
  // If not yet dispatched, onSuccess is called synchronously during decodeHeaders, not in an async
  // token request. Set state_ for the caller to check instead of calling continueDecoding.
  const bool is_request_dispatched = (in_flight_request_ != nullptr);
  in_flight_request_ = nullptr;

  if (parent_ == nullptr) {
    state_ = OAuthState::Idle;
    return;
  }

  ASSERT(state_ == OAuthState::PendingAccessToken ||
         state_ == OAuthState::PendingAccessTokenByRefreshToken ||
         state_ == OAuthState::PendingPushedAuthorizationRequest);
  const OAuthState oldState = state_;
  state_ = OAuthState::Idle;

  // Check that the auth cluster returned a happy response.
  const absl::string_view response_code = message->headers().getStatusValue();

  // RFC 9126 section 2.2: a successful PAR response is 201 Created.
  const bool is_par_success =
      oldState == OAuthState::PendingPushedAuthorizationRequest && response_code == "201";
  if (response_code != "200" && !is_par_success) {
    const std::string response_body = message->bodyAsString();
    ENVOY_TAGGED_STREAM_LOG(debug, oauthLogTags(*decoder_callbacks_), *decoder_callbacks_,
                            "Oauth response code: {}", response_code);
    ENVOY_TAGGED_STREAM_LOG(debug, oauthLogTags(*decoder_callbacks_), *decoder_callbacks_,
                            "Oauth response body: {}", response_body);
    switch (oldState) {
    case OAuthState::PendingAccessToken:
      handleOAuthFailure(
          is_request_dispatched, "Failed to get access token",
          fmt::format("response code: {}, response body: {}", response_code, response_body));
      break;
    case OAuthState::PendingAccessTokenByRefreshToken:
      handleRefreshTokenFailure(is_request_dispatched);
      break;
    case OAuthState::PendingPushedAuthorizationRequest:
      ENVOY_TAGGED_STREAM_LOG(debug, oauthLogTags(*decoder_callbacks_), *decoder_callbacks_,
                              "PAR request failed with response code: {}", response_code);
      parent_->onPushAuthorizationRequestFailure();
      break;
    default:
      PANIC("Malformed oauth client state");
    }
    return;
  }

  const std::string response_body = message->bodyAsString();

  if (oldState == OAuthState::PendingPushedAuthorizationRequest) {
    if (parsePushedAuthorizationResponse(response_body)) {
      parent_->onPushAuthorizationRequestSuccess();
    } else {
      parent_->onPushAuthorizationRequestFailure();
    }
    return;
  }

  envoy::extensions::http_filters::oauth2::OAuthResponse response;
  TRY_NEEDS_AUDIT {
    MessageUtil::loadFromJson(response_body, response, ProtobufMessage::getNullValidationVisitor());
  }
  END_TRY catch (EnvoyException& e) {
    handleOAuthFailure(is_request_dispatched, "Failed to parse oauth response body",
                       fmt::format("response body: {}, exception: {}", response_body, e.what()));
    return;
  }

  // TODO(snowp): Should this be a pgv validation instead? A more readable log
  // message might be good enough reason to do this manually?
  if (!response.has_access_token()) {
    handleOAuthFailure(is_request_dispatched,
                       "No access token found in the token exchange response",
                       fmt::format("response body: {}", response_body));
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
    handleOAuthFailure(
        is_request_dispatched,
        "No default or explicit access token expiration found in the token exchange response",
        fmt::format("response body: {}", response_body));
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
  // If not yet dispatched, onFailure was called synchronously during decodeHeaders rather than in
  // an async token request. Set state_ for the caller to check instead of calling continueDecoding.
  const bool is_request_dispatched = (in_flight_request_ != nullptr);
  in_flight_request_ = nullptr;

  if (parent_ == nullptr) {
    state_ = OAuthState::Idle;
    return;
  }

  ENVOY_TAGGED_STREAM_LOG(debug, oauthLogTags(*decoder_callbacks_), *decoder_callbacks_,
                          "OAuth request failed.");
  const OAuthState oldState = state_;
  state_ = OAuthState::Idle;

  switch (oldState) {
  case OAuthState::PendingAccessToken:
    handleOAuthFailure(is_request_dispatched,
                       "Failed to get access token due to HTTP request failure");
    break;
  case OAuthState::PendingAccessTokenByRefreshToken:
    handleRefreshTokenFailure(is_request_dispatched);
    break;
  case OAuthState::PendingPushedAuthorizationRequest:
    parent_->onPushAuthorizationRequestFailure();
    break;
  default:
    PANIC("Malformed oauth client state");
  }
}

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
