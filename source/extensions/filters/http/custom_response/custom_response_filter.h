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

  Http::RequestHeaderMap* downstreamHeaders() { return downstream_headers_; }
  Http::StreamEncoderFilterCallbacks* encoderCallbacks() { return encoder_callbacks_; }
  Http::StreamDecoderFilterCallbacks* decoderCallbacks() { return decoder_callbacks_; }

  ~CustomResponseFilter() override = default;

  CustomResponseFilter(std::shared_ptr<FilterConfig> config)
      : config_{std::move(config)}, config_to_use_{config_.get()} {}

private:
  std::shared_ptr<FilterConfig> config_;
  const FilterConfig* config_to_use_ = nullptr;
  Http::RequestHeaderMap* downstream_headers_ = nullptr;
};

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
