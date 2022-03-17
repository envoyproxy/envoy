#pragma once
#include <memory>
#include <string>

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.validate.h"

#include "source/common/http/message_impl.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/gcp_authn/gcp_authn_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

using FilterConfigProtoSharedPtr =
    std::shared_ptr<envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig>;

class GcpAuthnFilter : public Http::PassThroughFilter,
                       public RequestCallbacks,
                       public Logger::Loggable<Logger::Id::filter> {
public:
  // State of this filter's communication with the external authorization service.
  // The filter has either not started calling the external service, in the middle of calling
  // it or has completed.
  enum class State { NotStarted, Calling, Complete };
  GcpAuthnFilter(
      const envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig& config,
      Server::Configuration::FactoryContext& context)
      : filter_config_(
            std::make_shared<envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig>(
                config)),
        context_(context), client_(std::make_unique<GcpAuthnClient>(*filter_config_, context_)) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

  void onDestroy() override;
  void onComplete(const Http::ResponseMessage* response) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;
  State getState() { return state_; }
  ~GcpAuthnFilter() override = default;

private:
  FilterConfigProtoSharedPtr filter_config_;
  Server::Configuration::FactoryContext& context_;
  std::unique_ptr<GcpAuthnClient> client_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};

  bool initiating_call_{};
  State state_{State::NotStarted};
};

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
