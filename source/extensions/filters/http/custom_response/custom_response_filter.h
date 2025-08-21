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

class CustomResponseFilter : public ::Envoy::Http::PassThroughFilter,
                             public Logger::Loggable<Logger::Id::filter> {
public:
  ::Envoy::Http::FilterHeadersStatus decodeHeaders(::Envoy::Http::RequestHeaderMap& header_map,
                                                   bool) override;

  ::Envoy::Http::FilterHeadersStatus encodeHeaders(::Envoy::Http::ResponseHeaderMap& headers,
                                                   bool end_stream) override;
  void setEncoderFilterCallbacks(::Envoy::Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }

  void setDecoderFilterCallbacks(::Envoy::Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  ::Envoy::Http::LocalErrorStatus
  onLocalReply(const ::Envoy::Http::StreamFilterBase::LocalReplyData&) override {
    on_local_reply_called_ = true;
    return ::Envoy::Http::LocalErrorStatus::Continue;
  }

  ::Envoy::Http::RequestHeaderMap* downstreamHeaders() { return downstream_headers_; }
  ::Envoy::Http::StreamEncoderFilterCallbacks* encoderCallbacks() { return encoder_callbacks_; }
  ::Envoy::Http::StreamDecoderFilterCallbacks* decoderCallbacks() { return decoder_callbacks_; }
  bool onLocalReplyCalled() const { return on_local_reply_called_; }

  ~CustomResponseFilter() override = default;

  CustomResponseFilter(std::shared_ptr<FilterConfig> config) : config_{std::move(config)} {}

private:
  const std::shared_ptr<const FilterConfig> config_;
  ::Envoy::Http::RequestHeaderMap* downstream_headers_ = nullptr;
  bool on_local_reply_called_ = false;
};

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
