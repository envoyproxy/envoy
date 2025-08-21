#pragma once

#include "envoy/http/filter.h"
#include "envoy/network/address.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/original_src/config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OriginalSrc {

/**
 * Implements the Original Src http filter. This filter places the downstream source address of the
 * request, as determined by the stream's `downstreamRemoteAddress()`, into an option which will  be
 * used to partition upstream connections. This does not support non-ip (e.g. AF_UNIX) connections;
 * they will use the same address they would have had this filter not been installed.
 */
class OriginalSrcFilter : public Http::StreamDecoderFilter, Logger::Loggable<Logger::Id::filter> {
public:
  explicit OriginalSrcFilter(const Config& config);

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

private:
  Config config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
};

} // namespace OriginalSrc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
