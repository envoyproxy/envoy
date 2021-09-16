#pragma once

#include "envoy/http/filter.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "library/common/extensions/filters/http/socket_selection/filter.pb.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SocketSelection {

/**
 * Filter to set upstream socket options based on network conditions.
 */
class SocketSelectionFilter final : public Http::PassThroughFilter,
                                    public Logger::Loggable<Logger::Id::filter> {
public:
  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
};

} // namespace SocketSelection
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
