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

/**
 * Possible async results for a fetchToken call.
 */
enum class ResponseStatus {
  //
  OK,
  // The gcp authentication service could not be queried.
  Error,
};

/**
 * Async callbacks used during fetchToken() calls.
 */
class RequestCallbacks {
public:
  virtual ~RequestCallbacks() = default;

  /**
   * Called on completion of request.
   *
   * @param status the status of the request.
   */
  virtual void onComplete(ResponseStatus status, const Http::ResponseMessage* response) PURE;
};

Http::RequestMessagePtr buildRequest(const std::string& method, const std::string& server_url);

class GcpAuthnClient : public Http::AsyncClient::Callbacks,
                       public Logger::Loggable<Logger::Id::init> {
public:
  GcpAuthnClient(
      const envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig& config,
      Server::Configuration::FactoryContext& context)
      : config_(config), context_(context) {}

  // TODO(tyxia) Copy Move constructor
  ~GcpAuthnClient() override {
    if (active_request_) {
      active_request_->cancel();
      active_request_ = nullptr;
    }
  }

  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override {}

  void fetchToken(RequestCallbacks& callbacks);

  void cancel() {
    if (active_request_) {
      active_request_->cancel();
      active_request_ = nullptr;
    }
  }

  // Http::AsyncClient::Callbacks implemented by this class.
  void onSuccess(const Http::AsyncClient::Request& request,
                 Http::ResponseMessagePtr&& response) override;
  void onFailure(const Http::AsyncClient::Request& request,
                 Http::AsyncClient::FailureReason reason) override;

private:
  void onError();
  // TODO(tyxia) Duplicated with cancel, delete
  // void resetRequest() {
  //   if (active_request_) {
  //     active_request_->cancel();
  //     active_request_ = nullptr;
  //   }
  // }
  envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig config_;
  Server::Configuration::FactoryContext& context_;
  Http::AsyncClient::Request* active_request_{};
  RequestCallbacks* callbacks_{};
};

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
