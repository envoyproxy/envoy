#include "common/stats/source_impl.h"

#include <vector>

namespace Envoy {
namespace Stats {

std::vector<CounterSharedPtr>& SourceImpl::cachedCounters() {
  if (!counters_) {
    counters_ = store_.counters();
  }
  return *counters_;
}
std::vector<GaugeSharedPtr>& SourceImpl::cachedGauges() {
  if (!gauges_) {
    gauges_ = store_.gauges();
  }
  return *gauges_;
}
std::vector<BoolIndicatorSharedPtr>& SourceImpl::cachedBoolIndicators() {
  if (!bool_indicators_) {
    bool_indicators_ = store_.boolIndicators();
  }
  return *bool_indicators_;
}
std::vector<ParentHistogramSharedPtr>& SourceImpl::cachedHistograms() {
  if (!histograms_) {
    histograms_ = store_.histograms();
  }
  return *histograms_;
}

void SourceImpl::clearCache() {
  counters_.reset();
  gauges_.reset();
  bool_indicators_.reset();
  histograms_.reset();
}

} // namespace Stats
} // namespace Envoy
