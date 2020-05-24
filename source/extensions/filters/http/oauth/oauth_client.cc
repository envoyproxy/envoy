#include "extensions/filters/http/oauth/oauth_client.h"

#include <chrono>

#include "envoy/http/async_client.h"
#include "envoy/http/message.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/fmt.h"
#include "common/common/logger.h"
#include "common/http/message_impl.h"
#include "common/json/json_loader.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth {

constexpr absl::string_view authTokenEndpoint = "/oauth/token/";

// JSON keys for the various responses
const std::string& kAccessToken() { CONSTRUCT_ON_FIRST_USE(std::string, "access_token"); }

const std::string& kExpiresIn() { CONSTRUCT_ON_FIRST_USE(std::string, "expires_in"); }

const std::string& getAccessTokenBodyFormatString() {
  CONSTRUCT_ON_FIRST_USE(
      std::string,
      "grant_type=authorization_code&code={0}&client_id={1}&client_secret={2}&redirect_uri={3}");
}

void OAuth2ClientImpl::asyncGetAccessToken(const std::string& auth_code,
                                           const std::string& client_id, const std::string& secret,
                                           const std::string& cb_url) {
  Http::RequestMessagePtr request = createPostRequest();
  request->headers().setPath(authTokenEndpoint);
  const std::string body =
      fmt::format(getAccessTokenBodyFormatString(), auth_code, client_id, secret, cb_url);
  request->body() = std::make_unique<Buffer::OwnedImpl>(body);

  ENVOY_LOG(debug, "Dispatching OAuth request for access token.");
  dispatchRequest(std::move(request));
  state_ = OAuthState::PendingAccessToken;
}

void OAuth2ClientImpl::dispatchRequest(Http::RequestMessagePtr&& msg) {
  in_flight_request_ = cm_.httpAsyncClientForCluster(cluster_name_)
                           .send(std::move(msg), *this,
                                 Http::AsyncClient::RequestOptions().setTimeout(timeout_duration_));
}

// successful request calls will end up here
void OAuth2ClientImpl::onSuccess(const Http::AsyncClient::Request&,
                                 Http::ResponseMessagePtr&& message) {
  in_flight_request_ = nullptr;

  /**
   * Due to the asynchronous nature of this client, it's important to immediately update the state
   * that we are not waiting on anything, otherwise a painful debugging session may ensue.
   */
  OAuthState prior_state = state_;
  state_ = OAuthState::Idle;

  // Check that the auth cluster returned a happy response.
  const auto response_code = message->headers().Status()->value().getStringView();
  if (response_code != "200") {
    ENVOY_LOG(debug, "Oauth response code: {}", response_code);
    parent_->sendUnauthorizedResponse();
    return;
  }

  const std::string response_body = message->bodyAsString();

  Json::ObjectSharedPtr json_object;
  try {
    json_object = Json::Factory::loadFromString(response_body);
  } catch (const Json::Exception& e) {
    ENVOY_LOG(debug, "Error parsing response body, received JSON Exception: {}", e.what());
    ENVOY_LOG(debug, "Response body: {}", response_body);
    parent_->sendUnauthorizedResponse();
    return;
  }
  if (json_object == nullptr) {
    ENVOY_LOG(debug, "No json body was loaded.");
    parent_->sendUnauthorizedResponse();
    return;
  }
  // The current state of the client dictates how we interpret the response.
  if (prior_state == OAuthState::PendingAccessToken) {
    if (!json_object->hasObject(kAccessToken()) || !json_object->hasObject(kExpiresIn())) {
      ENVOY_LOG(debug, "No access token or expiration after asyncGetAccessToken");
      parent_->sendUnauthorizedResponse();
      return;
    }

    /**
     * Even though we've validated the existence of the objects above, one possibility is that the
     * `expires_in` object is not a valid integer, indicative of a problem on the OAuth server.
     * In this case, we use a default value of "0" and carry on (rather than a try/catch construct),
     * as this default value would cause authentication to fail when the user tries to validate
     * their cookies.
     */
    const std::string access_token = json_object->getString(kAccessToken());
    const std::string new_expires = std::to_string(json_object->getInteger(kExpiresIn(), 0));
    parent_->onGetAccessTokenSuccess(access_token, new_expires);
  }
}

// failed request calls will end up here
void OAuth2ClientImpl::onFailure(const Http::AsyncClient::Request&,
                                 Http::AsyncClient::FailureReason) {
  ENVOY_LOG(debug, "OAuth request failed.");
  in_flight_request_ = nullptr;
  parent_->sendUnauthorizedResponse();
}

} // namespace Oauth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
