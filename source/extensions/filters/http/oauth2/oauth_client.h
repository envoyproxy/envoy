#pragma once

#include <string>
#include <utility>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/config/core/v3/http_uri.pb.h"
#include "envoy/http/async_client.h"
#include "envoy/http/filter.h"
#include "envoy/http/message.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/oauth2/oauth.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth2 {

using RouteRetryPolicy = envoy::config::route::v3::RetryPolicy;
using HttpUri = envoy::config::core::v3::HttpUri;

/**
 * An OAuth client abstracts away everything regarding how to communicate with
 * the OAuth server. The filter should only need to invoke the functions here,
 * and then wait in a `StopIteration` mode until a callback is triggered.
 */
class OAuth2Client : public Http::AsyncClient::Callbacks {
public:
  // Tracks the state of the OAuth client across a single request lifecycle.
  enum class OAuthState {
    Idle,
    PendingAccessToken,               // Async request for access token is in-flight.
    PendingAccessTokenByRefreshToken, // Async request to refresh access token is in-flight.
    PendingPushedAuthorizationRequest, // Async PAR request (RFC 9126) is in-flight.
    FailureContinue, // Request failed; caller should continue decoding (allow-failed path).
    FailureStop,     // Request failed; caller should stop (local reply or redirect already sent).
  };

  virtual void asyncGetAccessToken(const std::string& auth_code, const std::string& client_id,
                                   const std::string& secret, const std::string& cb_url,
                                   const std::string& code_verifier,
                                   AuthType auth_type = AuthType::UrlEncodedBody) PURE;

  virtual void asyncRefreshAccessToken(const std::string& refresh_token,
                                       const std::string& client_id, const std::string& secret,
                                       AuthType auth_type = AuthType::UrlEncodedBody) PURE;

  // Raw (unencoded) key/value pairs. The client percent-encodes them.
  using FormParams = std::vector<std::pair<std::string, std::string>>;

  // Sends the authorization parameters to the PAR endpoint. On success, the returned
  // request_uri is available from pushedRequestUri().
  virtual void asyncPushAuthorizationRequest(const FormParams& params,
                                             const std::string& client_id,
                                             const std::string& secret,
                                             AuthType auth_type = AuthType::UrlEncodedBody) PURE;

  virtual const std::string& pushedRequestUri() const PURE;

  virtual void setCallbacks(FilterCallbacks& callbacks) PURE;
  virtual void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) PURE;

  virtual OAuthState getState() const PURE;

  /**
   * Cancels any in-flight token request and detaches from the filter so late async completions
   * cannot invoke filter callbacks or decoder callbacks after stream teardown.
   */
  virtual void cancel() PURE;

  // Http::AsyncClient::Callbacks
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& m) override PURE;
  void onFailure(const Http::AsyncClient::Request&,
                 Http::AsyncClient::FailureReason f) override PURE;
};

class OAuth2ClientImpl : public OAuth2Client, Logger::Loggable<Logger::Id::oauth2> {
public:
  // `uri` is owned by the filter config, which outlives the client, so it is referenced rather
  // than copied: copying the proto message on every client creation is not free.
  OAuth2ClientImpl(Upstream::ClusterManager& cm, const HttpUri& uri,
                   Router::RetryPolicyConstSharedPtr retry_policy,
                   const std::chrono::seconds default_expires_in)
      : cm_(cm), uri_(uri), retry_policy_(std::move(retry_policy)),
        default_expires_in_(default_expires_in) {}

  ~OAuth2ClientImpl() override { cancel(); }

  // OAuth2Client
  void asyncGetAccessToken(const std::string& auth_code, const std::string& client_id,
                           const std::string& secret, const std::string& cb_url,
                           const std::string& code_verifier, AuthType auth_type) override;

  void asyncRefreshAccessToken(const std::string& refresh_token, const std::string& client_id,
                               const std::string& secret, AuthType auth_type) override;

  void asyncPushAuthorizationRequest(const FormParams& params, const std::string& client_id,
                                     const std::string& secret, AuthType auth_type) override;

  const std::string& pushedRequestUri() const override { return request_uri_; }

  // The PAR endpoint is owned by the filter config, like `uri`. Must be set before a PAR request.
  void setParEndpoint(const HttpUri& par_endpoint) { par_endpoint_ = &par_endpoint; }

  void setCallbacks(FilterCallbacks& callbacks) override { parent_ = &callbacks; }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  OAuthState getState() const override { return state_; }

  void cancel() override;

  // AsyncClient::Callbacks
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& m) override;
  void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason f) override;

  void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&,
                                    const Http::ResponseHeaderMap*) override {}

private:
  friend class OAuth2ClientTest;

  FilterCallbacks* parent_{nullptr};
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{nullptr};

  Upstream::ClusterManager& cm_;
  const HttpUri& uri_;
  const Router::RetryPolicyConstSharedPtr retry_policy_;
  const std::chrono::seconds default_expires_in_;

  // Tracks any outstanding in-flight requests, allowing us to cancel the request
  // if the filter ends before the request completes.
  Http::AsyncClient::Request* in_flight_request_{nullptr};

  // Due to the asynchronous nature of this functionality, it is helpful to have managed state which
  // is tracked here.
  OAuthState state_{OAuthState::Idle};

  // PAR endpoint from the filter config. Null until setParEndpoint() is called.
  const HttpUri* par_endpoint_{nullptr};
  // The request_uri returned by the last successful PAR request.
  std::string request_uri_;

  /**
   * Begins execution of an asynchronous request.
   *
   * @param request the HTTP request to be executed.
   * @param uri the endpoint (cluster and timeout) to send the request to.
   */
  void dispatchRequest(Http::RequestMessagePtr&& request, const HttpUri& uri);

  // Parses the PAR response body and stores request_uri_. Returns false if it is missing.
  bool parsePushedAuthorizationResponse(const std::string& response_body);

  // Calls handleOAuthFailure and either calls continueDecoding() (async path) or sets state_
  // (sync path) depending on whether the request was already dispatched.
  void handleOAuthFailure(bool is_request_dispatched, const std::string& reason,
                          const std::string& extra_details = "");

  // Calls onRefreshAccessTokenFailure and either calls continueDecoding() (async path) or sets
  // state_ (sync path) depending on whether the request was already dispatched.
  void handleRefreshTokenFailure(bool is_request_dispatched);

  Http::RequestMessagePtr createPostRequest(const HttpUri& uri) {
    auto request = Http::Utility::prepareHeaders(uri);
    request->headers().setReferenceMethod(Http::Headers::get().MethodValues.Post);
    request->headers().setReferenceContentType(
        Http::Headers::get().ContentTypeValues.FormUrlEncoded);
    // Use the Accept header to ensure the Access Token Response is returned as JSON.
    // Some authorization servers return other encodings (e.g. FormUrlEncoded) in the absence of the
    // Accept header. RFC 6749 Section 5.1 defines the media type to be JSON, so this is safe.
    request->headers().setReference(Http::CustomHeaders::get().Accept,
                                    Http::Headers::get().ContentTypeValues.Json);
    return request;
  }
};

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
