#include "source/extensions/http/injected_credentials/oauth2/oauth_client.h"

#include <chrono>

#include "envoy/http/async_client.h"
#include "envoy/http/message.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/common/logger.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/http/injected_credentials/oauth2/oauth_response.pb.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace InjectedCredentials {
namespace OAuth2 {

namespace {
constexpr const char* GetAccessTokenBodyFormatString =
    "grant_type=client_credentials&client_id={0}&client_secret={1}";
constexpr const char* GetAccessTokenBodyFormatStringWithScopes =
    "grant_type=client_credentials&client_id={0}&client_secret={1}&scope={2}";

} // namespace

OAuth2Client::GetTokenResult OAuth2ClientImpl::asyncGetAccessToken(const std::string& client_id,
                                                                   const std::string& secret,
                                                                   const std::string& scopes) {
  if (in_flight_request_ != nullptr) {
    return GetTokenResult::NotDispatchedAlreadyInFlight;
  }
  const auto encoded_client_id = Envoy::Http::Utility::PercentEncoding::encode(client_id, ":/=&?");
  const auto encoded_secret = Envoy::Http::Utility::PercentEncoding::encode(secret, ":/=&?");

  Envoy::Http::RequestMessagePtr request = createPostRequest();
  const std::string body =
      fmt::format(GetAccessTokenBodyFormatString, encoded_client_id, encoded_secret);
  if (!(scopes.empty())) {
    const auto encoded_scopes = Envoy::Http::Utility::PercentEncoding::encode(scopes, ":/=&?");
    const std::string body = fmt::format(GetAccessTokenBodyFormatStringWithScopes,
                                         encoded_client_id, encoded_secret, encoded_scopes);
  }
  request->body().add(body);
  request->headers().setContentLength(body.length());
  return dispatchRequest(std::move(request));
}

OAuth2Client::GetTokenResult
OAuth2ClientImpl::dispatchRequest(Envoy::Http::RequestMessagePtr&& msg) {
  const auto thread_local_cluster = cm_.getThreadLocalCluster(uri_.cluster());
  if (thread_local_cluster != nullptr) {
    in_flight_request_ = thread_local_cluster->httpAsyncClient().send(
        std::move(msg), *this,
        Envoy::Http::AsyncClient::RequestOptions().setTimeout(
            std::chrono::milliseconds(PROTOBUF_GET_MS_REQUIRED(uri_, timeout))));
  } else {
    return GetTokenResult::NotDispatchedClusterNotFound;
  }
  return GetTokenResult::DispatchedRequest;
}

void OAuth2ClientImpl::onSuccess(const Envoy::Http::AsyncClient::Request&,
                                 Envoy::Http::ResponseMessagePtr&& message) {
  in_flight_request_ = nullptr;
  // Check that the auth cluster returned a happy response.
  const auto response_code = message->headers().Status()->value().getStringView();
  if (response_code != "200") {
    ENVOY_LOG(error, "Oauth response code: {}", response_code);
    ENVOY_LOG(error, "Oauth response body: {}", message->bodyAsString());
    parent_->onGetAccessTokenFailure(FilterCallbacks::FailureReason::BadResponseCode);
    return;
  }

  const std::string response_body = message->bodyAsString();

  envoy::extensions::http::injected_credentials::oauth2::OAuthResponse response;
  TRY_NEEDS_AUDIT {
    MessageUtil::loadFromJson(response_body, response, ProtobufMessage::getNullValidationVisitor());
  }
  END_TRY catch (EnvoyException& e) {
    ENVOY_LOG(error, "Error parsing response body, received exception: {}", e.what());
    ENVOY_LOG(error, "Response body: {}", response_body);
    parent_->onGetAccessTokenFailure(FilterCallbacks::FailureReason::BadToken);
    return;
  }

  if (!response.has_access_token() || !response.has_expires_in()) {
    ENVOY_LOG(error, "No access token or expiration after asyncGetAccessToken");
    parent_->onGetAccessTokenFailure(FilterCallbacks::FailureReason::BadToken);
    return;
  }

  const std::string access_token{PROTOBUF_GET_WRAPPED_REQUIRED(response, access_token)};
  const std::chrono::seconds expires_in{PROTOBUF_GET_WRAPPED_REQUIRED(response, expires_in)};
  parent_->onGetAccessTokenSuccess(access_token, expires_in);
}

void OAuth2ClientImpl::onFailure(const Envoy::Http::AsyncClient::Request&,
                                 Envoy::Http::AsyncClient::FailureReason) {
  ENVOY_LOG(error, "OAuth request failed: stream reset");
  in_flight_request_ = nullptr;
  parent_->onGetAccessTokenFailure(FilterCallbacks::FailureReason::StreamReset);
}

} // namespace OAuth2
} // namespace InjectedCredentials
} // namespace Http
} // namespace Extensions
} // namespace Envoy
