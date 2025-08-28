#pragma once

#include "envoy/http/filter.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "contrib/envoy/extensions/load_balancing_policies/peak_ewma/v3alpha/source/peak_ewma_lb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PeakEwma {

class PeakEwmaRttFilter : public Http::PassThroughFilter {
public:
  // Override encode headers to capture RTT
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
};

} // namespace PeakEwma
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
