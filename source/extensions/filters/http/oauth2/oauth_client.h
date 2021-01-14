#pragma once

#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/core/v3/http_uri.pb.h"
#include "envoy/http/async_client.h"
#include "envoy/http/message.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"

#include "extensions/filters/http/oauth2/oauth.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth2 {

/**
 * An OAuth client abstracts away everything regarding how to communicate with
 * the OAuth server. The filter should only need to invoke the functions here,
 * and then wait in a `StopIteration` mode until a callback is triggered.
 */
class OAuth2Client : public Http::AsyncClient::Callbacks {
public:
  virtual void asyncGetAccessToken(const std::string& auth_code, const std::string& client_id,
                                   const std::string& secret, const std::string& cb_url) PURE;
  virtual void setCallbacks(FilterCallbacks& callbacks) PURE;

  // Http::AsyncClient::Callbacks
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& m) override PURE;
  void onFailure(const Http::AsyncClient::Request&,
                 Http::AsyncClient::FailureReason f) override PURE;
};

class OAuth2ClientImpl : public OAuth2Client, Logger::Loggable<Logger::Id::upstream> {
public:
  OAuth2ClientImpl(Upstream::ClusterManager& cm, const envoy::config::core::v3::HttpUri& uri)
      : cm_(cm), uri_(uri) {}

  ~OAuth2ClientImpl() override {
    if (in_flight_request_ != nullptr) {
      in_flight_request_->cancel();
    }
  }

  // OAuth2Client
  /**
   * Request the access token from the OAuth server. Calls the `onSuccess` on `onFailure` callbacks.
   */
  void asyncGetAccessToken(const std::string& auth_code, const std::string& client_id,
                           const std::string& secret, const std::string& cb_url) override;

  void setCallbacks(FilterCallbacks& callbacks) override { parent_ = &callbacks; }

  // AsyncClient::Callbacks
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& m) override;
  void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason f) override;
  void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&,
                                    const Http::ResponseHeaderMap*) override {}

private:
  friend class OAuth2ClientTest;

  FilterCallbacks* parent_{nullptr};

  Upstream::ClusterManager& cm_;
  const envoy::config::core::v3::HttpUri uri_;

  // Tracks any outstanding in-flight requests, allowing us to cancel the request
  // if the filter ends before the request completes.
  Http::AsyncClient::Request* in_flight_request_{nullptr};

  enum class OAuthState { Idle, PendingAccessToken };

  // Due to the asynchronous nature of this functionality, it is helpful to have managed state which
  // is tracked here.
  OAuthState state_{OAuthState::Idle};

  /**
   * Begins execution of an asynchronous request.
   *
   * @param request the HTTP request to be executed.
   */
  void dispatchRequest(Http::RequestMessagePtr&& request);

  Http::RequestMessagePtr createPostRequest() {
    auto request = Http::Utility::prepareHeaders(uri_);
    request->headers().setReferenceMethod(Http::Headers::get().MethodValues.Post);
    request->headers().setContentType(Http::Headers::get().ContentTypeValues.FormUrlEncoded);
    return request;
  }
};

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
