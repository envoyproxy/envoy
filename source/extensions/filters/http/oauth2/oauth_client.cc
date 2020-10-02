#include "extensions/filters/http/oauth2/oauth_client.h"

#include <chrono>

#include "envoy/http/async_client.h"
#include "envoy/http/message.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/fmt.h"
#include "common/common/logger.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/protobuf/message_validator_impl.h"
#include "common/protobuf/utility.h"

#include "source/extensions/filters/http/oauth2/oauth_response.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth2 {

namespace {
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    authorization_handle(Http::CustomHeaders::get().Authorization);

constexpr const char* GetAccessTokenBodyFormatString =
    "grant_type=authorization_code&code={0}&client_id={1}&client_secret={2}&redirect_uri={3}";

} // namespace

void OAuth2ClientImpl::asyncGetAccessToken(const std::string& auth_code,
                                           const std::string& client_id, const std::string& secret,
                                           const std::string& cb_url) {
  const auto encoded_client_id = Http::Utility::PercentEncoding::encode(client_id, ":/=&?");
  const auto encoded_secret = Http::Utility::PercentEncoding::encode(secret, ":/=&?");
  const auto encoded_cb_url = Http::Utility::PercentEncoding::encode(cb_url, ":/=&?");

  Http::RequestMessagePtr request = createPostRequest();
  const std::string body = fmt::format(GetAccessTokenBodyFormatString, auth_code, encoded_client_id,
                                       encoded_secret, encoded_cb_url);
  ENVOY_LOG(debug, "Dispatching OAuth request for access token.");
  dispatchRequest(std::move(request));

  ASSERT(state_ == OAuthState::Idle);
  state_ = OAuthState::PendingAccessToken;
}

void OAuth2ClientImpl::dispatchRequest(Http::RequestMessagePtr&& msg) {
  in_flight_request_ =
      cm_.httpAsyncClientForCluster(uri_.cluster())
          .send(std::move(msg), *this,
                Http::AsyncClient::RequestOptions().setTimeout(
                    std::chrono::milliseconds(PROTOBUF_GET_MS_REQUIRED(uri_, timeout))));
}

void OAuth2ClientImpl::onSuccess(const Http::AsyncClient::Request&,
                                 Http::ResponseMessagePtr&& message) {
  in_flight_request_ = nullptr;

  ASSERT(state_ == OAuthState::PendingAccessToken);
  state_ = OAuthState::Idle;

  // Check that the auth cluster returned a happy response.
  const auto response_code = message->headers().Status()->value().getStringView();
  if (response_code != "200") {
    ENVOY_LOG(debug, "Oauth response code: {}", response_code);
    parent_->sendUnauthorizedResponse();
    return;
  }

  const std::string response_body = message->bodyAsString();

  envoy::extensions::http_filters::oauth2::OAuthResponse response;
  try {
    MessageUtil::loadFromJson(response_body, response, ProtobufMessage::getNullValidationVisitor());
  } catch (EnvoyException& e) {
    ENVOY_LOG(debug, "Error parsing response body, received exception: {}", e.what());
    ENVOY_LOG(debug, "Response body: {}", response_body);
    parent_->sendUnauthorizedResponse();
    return;
  }

  // TODO(snowp): Should this be a pgv validation instead? A more readable log
  // message might be good enough reason to do this manually?
  if (!response.has_access_token() || !response.has_expires_in()) {
    ENVOY_LOG(debug, "No access token or expiration after asyncGetAccessToken");
    parent_->sendUnauthorizedResponse();
    return;
  }

  const std::string access_token{PROTOBUF_GET_WRAPPED_REQUIRED(response, access_token)};
  const std::chrono::seconds expires_in{PROTOBUF_GET_WRAPPED_REQUIRED(response, expires_in)};
  parent_->onGetAccessTokenSuccess(access_token, expires_in);
}

void OAuth2ClientImpl::onFailure(const Http::AsyncClient::Request&,
                                 Http::AsyncClient::FailureReason) {
  ENVOY_LOG(debug, "OAuth request failed.");
  in_flight_request_ = nullptr;
  parent_->sendUnauthorizedResponse();
}

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
