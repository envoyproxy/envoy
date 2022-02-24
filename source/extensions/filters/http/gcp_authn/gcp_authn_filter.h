#pragma once
#include <memory>
#include <string>

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.validate.h"

#include "source/common/http/message_impl.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/gcp_authn/gcp_authn_filter.h"

// #include "source/extensions/filters/http/gcp_authn/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthentication {

using Server::Configuration::FactoryContext;

using FilterConfigProtoSharedPtr =
    std::shared_ptr<envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig>;

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

  void sendRequest(const std::string& method) {
    if (active_request_) {
      active_request_->cancel();
      active_request_ = nullptr;
    }

    const std::chrono::milliseconds fetch_timeout(1000);
    Http::RequestMessagePtr request_message = buildRequest(method);
    const struct Envoy::Http::AsyncClient::RequestOptions options =
        Envoy::Http::AsyncClient::RequestOptions()
            .setTimeout(fetch_timeout)
            // Metadata server rejects X-Forwarded-For requests.
            // https://cloud.google.com/compute/docs/storing-retrieving-metadata#x-forwarded-for_header
            .setSendXff(false);
    const auto thread_local_cluster =
        context_.clusterManager().getThreadLocalCluster(config_.http_uri().cluster());

    if (thread_local_cluster) {
      active_request_ =
          thread_local_cluster->httpAsyncClient().send(std::move(request_message), *this, options);
    }
  }

private:
  // Http::AsyncClient::Callbacks implemented by this class.
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&&) override {}

  void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) override {
    if (active_request_) {
      active_request_->cancel();
    }
  }

  Http::RequestMessagePtr buildRequest(const std::string& method);

  // Http::RequestMessagePtr buildRequest(const std::string& method);
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
        context_(context) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

  // Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
  //                                         bool end_stream) override;

  ~GcpAuthnFilter() override = default;

private:
  std::unique_ptr<GcpAuthnClient> CreateGcpAuthnClient() {
    return std::make_unique<GcpAuthnClient>(*filter_config_, context_);
  }
  FilterConfigProtoSharedPtr filter_config_;
  Server::Configuration::FactoryContext& context_;
};

} // namespace GcpAuthentication
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy