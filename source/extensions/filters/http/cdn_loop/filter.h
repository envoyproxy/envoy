#pragma once

#include <string>

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CdnLoop {

class CdnLoopFilter : public Http::PassThroughDecoderFilter {
public:
  CdnLoopFilter(std::string cdn_id, int max_allowed_occurrences)
      : cdn_id_(std::move(cdn_id)), max_allowed_occurrences_(max_allowed_occurrences) {}
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

private:
  const std::string cdn_id_;
  const int max_allowed_occurrences_;
};

} // namespace CdnLoop
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
