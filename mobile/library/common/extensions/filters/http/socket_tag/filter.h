#pragma once

#include "envoy/http/filter.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "library/common/extensions/filters/http/socket_tag/filter.pb.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SocketTag {

/**
 * Filter to set upstream socket tags based on a request header.
 * See: https://source.android.com/devices/tech/datausage/tags-explained
 */
class SocketTagFilter final : public Http::PassThroughFilter,
                              public Logger::Loggable<Logger::Id::filter> {
public:
  // Http::PassThroughDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& request_headers, bool) override;
};

} // namespace SocketTag
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
