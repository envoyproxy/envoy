#pragma once

#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/core/v3/http_uri.pb.h"
#include "envoy/http/async_client.h"
#include "envoy/http/message.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/extensions/http/injected_credentials/oauth2/oauth.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace InjectedCredentials {
namespace OAuth2 {

/**
 * An OAuth client abstracts away everything regarding how to communicate with
 * the OAuth server. It is responsible for sending the request to the OAuth server
 * and handling the response.
 */
class OAuth2Client : public Envoy::Http::AsyncClient::Callbacks {
public:
  enum class GetTokenResult {
    NotDispatchedClusterNotFound,
    NotDispatchedAlreadyInFlight,
    DispatchedRequest,
  };
  virtual GetTokenResult asyncGetAccessToken(const std::string& client_id,
                                             const std::string& secret,
                                             const std::string& scopes) PURE;
  virtual void setCallbacks(FilterCallbacks& callbacks) PURE;

  // Http::AsyncClient::Callbacks
  void onSuccess(const Envoy::Http::AsyncClient::Request&,
                 Envoy::Http::ResponseMessagePtr&& m) override PURE;
  void onFailure(const Envoy::Http::AsyncClient::Request&,
                 Envoy::Http::AsyncClient::FailureReason f) override PURE;
};

class OAuth2ClientImpl : public OAuth2Client, Logger::Loggable<Logger::Id::credential_injector> {
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
  GetTokenResult asyncGetAccessToken(const std::string& client_id, const std::string& secret,
                                     const std::string& scopes) override;

  void setCallbacks(FilterCallbacks& callbacks) override { parent_ = &callbacks; }

  // AsyncClient::Callbacks
  void onSuccess(const Envoy::Http::AsyncClient::Request&,
                 Envoy::Http::ResponseMessagePtr&& m) override;
  void onFailure(const Envoy::Http::AsyncClient::Request&,
                 Envoy::Http::AsyncClient::FailureReason f) override;
  void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&,
                                    const Envoy::Http::ResponseHeaderMap*) override {}

private:
  FilterCallbacks* parent_{nullptr};
  Upstream::ClusterManager& cm_;
  const envoy::config::core::v3::HttpUri uri_;
  // Tracks any outstanding in-flight requests, allowing us to cancel the request
  // if the filter ends before the request completes.
  Envoy::Http::AsyncClient::Request* in_flight_request_{nullptr};
  /**
   * Begins execution of an asynchronous request.
   *
   * @param request the HTTP request to be executed.
   */
  GetTokenResult dispatchRequest(Envoy::Http::RequestMessagePtr&& request);
  Envoy::Http::RequestMessagePtr createPostRequest() {
    auto request = Envoy::Http::Utility::prepareHeaders(uri_);
    request->headers().setReferenceMethod(Envoy::Http::Headers::get().MethodValues.Post);
    request->headers().setReferenceContentType(
        Envoy::Http::Headers::get().ContentTypeValues.FormUrlEncoded);
    // Use the Accept header to ensure the Access Token Response is returned as JSON.
    // Some authorization servers return other encodings (e.g. FormUrlEncoded) in the absence of the
    // Accept header. RFC 6749 Section 5.1 defines the media type to be JSON, so this is safe.
    request->headers().setReference(Envoy::Http::CustomHeaders::get().Accept,
                                    Envoy::Http::Headers::get().ContentTypeValues.Json);
    return request;
  }
};

} // namespace OAuth2
} // namespace InjectedCredentials
} // namespace Http
} // namespace Extensions
} // namespace Envoy
