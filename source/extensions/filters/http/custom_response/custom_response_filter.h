#pragma once

#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.validate.h"
#include "envoy/http/filter.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/custom_response/config.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

class CustomResponseFilter : public Http::PassThroughFilter,
                             public Logger::Loggable<Logger::Id::filter> {
public:
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& header_map, bool) override;

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  ~CustomResponseFilter() override = default;

  CustomResponseFilter(std::shared_ptr<FilterConfig> config,
                       Server::Configuration::FactoryContext& context)
      : config_{std::move(config)}, base_config_{config_.get()}, factory_context_(context) {}

  void onRemoteResponse(Http::ResponseHeaderMap& headers, const ResponseSharedPtr& custom_response,
                        const Http::ResponseMessage* response);

private:
  std::shared_ptr<FilterConfig> config_;
  const FilterConfigBase* base_config_ = nullptr;
  Server::Configuration::FactoryContext& factory_context_;
  Http::RequestHeaderMap* downstream_headers_ = nullptr;
};

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
