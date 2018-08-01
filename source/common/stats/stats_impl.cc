#include "common/stats/stats_impl.h"

#include <string.h>

#include <algorithm>
#include <chrono>
#include <string>

#include "envoy/common/exception.h"

#include "common/common/lock_guard.h"
#include "common/common/perf_annotation.h"
#include "common/common/thread.h"
#include "common/common/utility.h"
#include "common/stats/utility.h"

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"

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
std::vector<ParentHistogramSharedPtr>& SourceImpl::cachedHistograms() {
  if (!histograms_) {
    histograms_ = store_.histograms();
  }
  return *histograms_;
}

void SourceImpl::clearCache() {
  counters_.reset();
  gauges_.reset();
  histograms_.reset();
}

} // namespace Stats
} // namespace Envoy
