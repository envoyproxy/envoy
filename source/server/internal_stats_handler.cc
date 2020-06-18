#include <iostream>
#include <map>
#include <set>

#include "envoy/server/internal_stats_handler.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/store.h"

namespace Envoy {
namespace Server {

void InternalStatsHandler::receiveGlobalStats(std::set<std::string>& stat_names,
                                              PostReceiveCb cb) const {
  // TODO(ramaraochavali): See the comment in ThreadLocalStoreImpl::histograms() for why we use a
  // multimap here. This makes sure that duplicate histograms get output. When shared storage is
  // implemented this can be switched back to a normal map.
  std::multimap<std::string, const Envoy::Stats::HistogramStatistics&> all_histograms;
  for (const Stats::ParentHistogramSharedPtr& histogram : store_root_.histograms()) {
    auto iterator = stat_names.find(histogram->name());
    if (iterator != stat_names.end()) {
      const Stats::HistogramStatistics& hist = histogram->intervalStatistics();
      all_histograms.emplace(histogram->name(), hist);
    }
  }
  cb(all_histograms);
}

} // namespace Server
} // namespace Envoy
