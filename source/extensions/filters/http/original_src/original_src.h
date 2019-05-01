#pragma once

#include "envoy/http/filter.h"
#include "envoy/network/address.h"

#include "common/common/logger.h"

#include "extensions/filters/http/original_src/config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OriginalSrc {

/**
 * Implements the Original Src network filter. This filter places the source address of the socket
 * into an option which will alter be used to partition upstream connections.
 * This does not support non-ip (e.g. AF_UNIX) connections, which will be failed and counted.
 */
class OriginalSrcFilter : public Http::StreamDecoderFilter, Logger::Loggable<Logger::Id::filter> {
public:
  OriginalSrcFilter(const Config& config);

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

private:
  Config config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
};

} // namespace OriginalSrc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
