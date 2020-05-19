#pragma once

#include <string>

#include "envoy/common/pure.h"
#include "envoy/http/async_client.h"
#include "envoy/http/message.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/http/message_impl.h"

#include "extensions/filters/http/oauth/oauth.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth {

enum class OAuthState { Idle, PendingAccessToken, PendingIdentity };

/*
  OAuth states
  - START
  - USER_AUTHORIZED (continue)
  - USER_UNAUTHORIZED
  - PENDING_ACCESS_TOKEN
  - PENDING_USER_IDENTITY
  - PENDING_AUTHORIZED_GROUPS

example flow:
START -> USER_UNAUTHORIZED -> PENDING_ACCESS_TOKEN -> PENDING_USER_IDENTITY ->
PENDING_AUTHORIZED_GROUPS -> USER_AUTHORIZED
*/

/**
 * An OAuth client abstracts away everything regarding how to communicate with
 * the OAuth server. The filter should only need to invoke the functions here,
 * and then wait in a `StopIteration` mode until a callback is triggered.
 */
class OAuth2Client : public Http::AsyncClient::Callbacks {
public:
  virtual void asyncGetAccessToken(const std::string& auth_code, const std::string& client_id,
                                   const std::string& secret, const std::string& cb_url) PURE;
  virtual void asyncGetIdentity(const std::string& access_token) PURE;
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& m) override PURE;
  void onFailure(const Http::AsyncClient::Request&,
                 Http::AsyncClient::FailureReason f) override PURE;
  virtual void setCallbacks(OAuth2FilterCallbacks& callbacks) PURE;
  ~OAuth2Client() override = default;
};

class OAuth2ClientImpl : public OAuth2Client, Logger::Loggable<Logger::Id::upstream> {
public:
  OAuth2ClientImpl(Upstream::ClusterManager& cm, std::string cluster_name,
                   const std::chrono::milliseconds timeout_duration)
      : cm_(cm), cluster_name_(std::move(cluster_name)), timeout_duration_(timeout_duration) {}

  ~OAuth2ClientImpl() override {
    if (in_flight_request_ != nullptr) {
      in_flight_request_->cancel();
    }
  }

  /**
   * Request the access token from the OAuth server. Calls the `onSuccess` on `onFailure` callbacks.
   */
  void asyncGetAccessToken(const std::string& auth_code, const std::string& client_id,
                           const std::string& secret, const std::string& cb_url) override;

  /**
   * Request the user's identity from the OAuth server. Calls the `onSuccess` on `onFailure`
   * callbacks.
   */
  void asyncGetIdentity(const std::string& access_token) override;

  void setCallbacks(OAuth2FilterCallbacks& callbacks) override { parent_ = &callbacks; }

  // AsyncClient::Callbacks
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& m) override;
  void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason f) override;

private:
  friend class OAuth2ClientTest;

  OAuth2FilterCallbacks* parent_{nullptr};

  // the cluster manager is required to get the HTTP client
  Upstream::ClusterManager& cm_;
  const std::string cluster_name_;
  const std::chrono::milliseconds timeout_duration_;

  // for simplicity we have one in-flight request at a time tracked via this pointer.
  Http::AsyncClient::Request* in_flight_request_{nullptr};

  // Due to the asynchronous nature of this functionality, it is helpful to have managed state which
  // is tracked here. Logic within a single filter is thread-safe so we don't have to worry about
  // locking the state in a mutex.
  OAuthState state_{OAuthState::Idle};

  /**
   * Begins execution of an asynchronous request.
   *
   * @param request the HTTP request to be executed.
   */
  void dispatchRequest(Http::RequestMessagePtr&& request);

  Http::RequestMessagePtr createBasicRequest() {
    Http::RequestMessagePtr request = std::make_unique<Http::RequestMessageImpl>();
    request->headers().setHost(cluster_name_);
    return request;
  }

  Http::RequestMessagePtr createAuthGetRequest(const std::string& access_token) {
    auto request = createBasicRequest();
    request->headers().setReferenceMethod(Http::Headers::get().MethodValues.Get);
    request->headers().setAuthorization(std::string("Bearer ") + access_token);
    return request;
  }

  Http::RequestMessagePtr createPostRequest() {
    static const std::string FormUrlEncodedContentType = "application/x-www-form-urlencoded";

    auto request = createBasicRequest();
    request->headers().setReferenceMethod(Http::Headers::get().MethodValues.Post);
    request->headers().setContentType(FormUrlEncodedContentType);
    return request;
  }
};

} // namespace Oauth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
