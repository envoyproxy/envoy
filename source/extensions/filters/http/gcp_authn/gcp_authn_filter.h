#pragma once
#include <memory>
#include <string>

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.validate.h"

#include "source/common/http/message_impl.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/gcp_authn/gcp_authn_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthentication {

using Server::Configuration::FactoryContext;

using FilterConfigProtoSharedPtr =
    std::shared_ptr<envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig>;

Http::RequestMessagePtr buildRequest(const std::string& method, const std::string& server_url);

class GcpAuthnClient : public Http::AsyncClient::Callbacks,
                       public Logger::Loggable<Logger::Id::init> {
public:
  GcpAuthnClient(
      const envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig& config,
      FactoryContext& context)
      : config_(config), context_(context) {}

  // TODO(tyxia) Copy Move constructor
  ~GcpAuthnClient() override {
    if (active_request_) {
      active_request_->cancel();
      active_request_ = nullptr;
    }
  }

  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override {}

  void fetchToken();

private:
  // Http::AsyncClient::Callbacks implemented by this class.
  void onSuccess(const Http::AsyncClient::Request& request,
                 Http::ResponseMessagePtr&& response) override;
  void onFailure(const Http::AsyncClient::Request& request,
                 Http::AsyncClient::FailureReason reason) override;
  void handleFailure();
  // TODO(tyxia) Any missing const??
  void resetRequest() {
    if (active_request_) {
      active_request_->cancel();
      active_request_ = nullptr;
    }
  }
  envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig config_;
  Server::Configuration::FactoryContext& context_;
  Http::AsyncClient::Request* active_request_{};
};

class GcpAuthnFilter : public Http::PassThroughFilter, public Logger::Loggable<Logger::Id::filter> {
public:
  GcpAuthnFilter(
      const envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig& config,
      FactoryContext& context)
      : filter_config_(
            std::make_shared<envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig>(
                config)),
        context_(context), client_(std::make_unique<GcpAuthnClient>(*filter_config_, context_)) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

  ~GcpAuthnFilter() override = default;

private:
  std::unique_ptr<GcpAuthnClient> CreateGcpAuthnClient() {
    return std::make_unique<GcpAuthnClient>(*filter_config_, context_);
  }
  FilterConfigProtoSharedPtr filter_config_;
  Server::Configuration::FactoryContext& context_;
  std::unique_ptr<GcpAuthnClient> client_;
  // TODO(tyxia) Add state
  // // State of this filter's communication with the external authorization service.
  // // The filter has either not started calling the external service, in the middle of calling
  // // it or has completed.
  // enum class State { NotStarted, Calling, Complete };
  // State state_{State::NotStarted};
};

} // namespace GcpAuthentication
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
