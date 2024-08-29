#pragma once
#include <memory>
#include <string>

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.validate.h"

#include "source/common/http/message_impl.h"
#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

constexpr absl::string_view UrlString =
    "http://metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/"
    "identity?audience=[AUDIENCE]";

Http::RequestMessagePtr buildRequest(absl::string_view url);

/**
 * Async callbacks used during fetchToken() calls.
 */
class RequestCallbacks {
public:
  virtual ~RequestCallbacks() = default;

  /**
   * Called on completion of request.
   *
   * @param response the pointer to the response message. Null response pointer means the request
   *        was completed with error.
   */
  virtual void onComplete(const Http::ResponseMessage* response_ptr) PURE;
};

class GcpAuthnClient : public Http::AsyncClient::Callbacks,
                       public Logger::Loggable<Logger::Id::init> {
public:
  GcpAuthnClient(
      const envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig& config,
      Server::Configuration::FactoryContext& context)
      : config_(config), context_(context) {}

  ~GcpAuthnClient() override { cancel(); }

  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override {}
  void fetchToken(RequestCallbacks& callbacks, Http::RequestMessagePtr&& request);
  void cancel();

  // Http::AsyncClient::Callbacks implemented by this class.
  void onSuccess(const Http::AsyncClient::Request& request,
                 Http::ResponseMessagePtr&& response) override;
  void onFailure(const Http::AsyncClient::Request& request,
                 Http::AsyncClient::FailureReason reason) override;

private:
  void onError();
  envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig config_;
  Server::Configuration::FactoryContext& context_;
  Http::AsyncClient::Request* active_request_{};
  RequestCallbacks* callbacks_{};
};

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
