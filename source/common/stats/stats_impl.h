#pragma once

#include <algorithm>
#include <functional>

#include "envoy/stats/stats.h"

#include "common/common/utility.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Stats {

// TODO(jmarantz): rename this to source_impl.h -- this will have a pretty large
// blast radius, as there are around ~60 references to this file.

class SourceImpl : public Source {
public:
  SourceImpl(Store& store) : store_(store){};

  // Stats::Source
  std::vector<CounterSharedPtr>& cachedCounters() override;
  std::vector<GaugeSharedPtr>& cachedGauges() override;
  std::vector<ParentHistogramSharedPtr>& cachedHistograms() override;
  void clearCache() override;

private:
  Store& store_;
  absl::optional<std::vector<CounterSharedPtr>> counters_;
  absl::optional<std::vector<GaugeSharedPtr>> gauges_;
  absl::optional<std::vector<ParentHistogramSharedPtr>> histograms_;
};

} // namespace Stats
} // namespace Envoy
