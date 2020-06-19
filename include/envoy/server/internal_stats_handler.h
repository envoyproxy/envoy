#pragma once
#include <map>
#include <set>

#include "envoy/stats/stats.h"
#include "envoy/stats/store.h"

namespace Envoy {
namespace Server {

using PostReceiveCb =
    std::function<void(std::multimap<std::string, const Stats::HistogramStatistics&> stats)>;

class InternalStatsHandler {

public:
  explicit InternalStatsHandler(Stats::StoreRoot& root) : store_root_(root) {}
  void receiveGlobalStats(std::set<std::string>& stat_names, PostReceiveCb cb) const;

private:
  Stats::StoreRoot& store_root_;
};

using InternalStatsHandlerPtr = std::unique_ptr<InternalStatsHandler>;

} // namespace Server
} // namespace Envoy