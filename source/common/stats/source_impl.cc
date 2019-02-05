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
std::vector<TextReadoutSharedPtr>& SourceImpl::cachedTextReadouts() {
  if (!text_readouts_) {
    text_readouts_ = store_.textReadouts();
  }
  return *text_readouts_;
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
  text_readouts_.reset();
  histograms_.reset();
}

} // namespace Stats
} // namespace Envoy
