#pragma once

#include "envoy/http/filter.h"
#include "envoy/network/address.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/sni_validation/config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SniValidation {

class SniValidationFilter : public Http::StreamDecoderFilter, Logger::Loggable<Logger::Id::filter> {
public:
  explicit SniValidationFilter(const Config& config);

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

private:
  Config config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
};

} // namespace SniValidation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
