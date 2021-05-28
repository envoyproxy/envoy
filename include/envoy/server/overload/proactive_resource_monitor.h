#pragma once

#include <string>

#include "envoy/common/pure.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Server {

class ProactiveResourceMonitor {
public:
  ProactiveResourceMonitor() = default;
  virtual ~ProactiveResourceMonitor() = default;
  /**
   * Tries to allocate resource for given resource monitor in thread safe manner.
   * Returns true if there is enough resource quota available and allocation has succeeded, false
   * otherwise.
   * @param increment to add to current resource usage value and compare against configured max
   * threshold.
   */
  virtual bool tryAllocateResource(uint64_t increment) PURE;
  /**
   * Tries to deallocate resource for given resource monitor in thread safe manner.
   * Returns true if there is enough resource quota available and deallocation has succeeded, false
   * otherwise.
   * @param decrement to subtract from current resource usage value.
   */
  virtual bool tryDeallocateResource(uint64_t decrement) PURE;
  /**
   * Returns current resource usage tracked by monitor.
   */
  virtual uint64_t currentResourceUsage() const PURE;
  /**
   * Returns max resource usage configured in monitor.
   */
  virtual uint64_t maxResourceUsage() const PURE;
};

using ProactiveResourceMonitorPtr = std::unique_ptr<ProactiveResourceMonitor>;

// Example of proactive resource monitor. To be removed.
class ActiveConnectionsResourceMonitor : public ProactiveResourceMonitor {
public:
  ActiveConnectionsResourceMonitor(uint64_t max_active_conns)
      : max_(max_active_conns), current_(0){};

  bool tryAllocateResource(uint64_t increment) {
    int64_t new_val = (current_ += increment);
    if (new_val > max_ || new_val < 0) {
      current_ -= increment;
      return false;
    }
    return true;
  }

  bool tryDeallocateResource(uint64_t decrement) {
    RELEASE_ASSERT(decrement <= current_,
                   "Cannot deallocate resource, current resource usage is lower than decrement");
    int64_t new_val = (current_ -= increment);
    if (new_val < 0) {
      current_ += increment;
      return false;
    }
    return true;
  }

  uint64_t currentResourceUsage() const { return current_.load(); }
  uint64_t maxResourceUsage() const { return max_; };

protected:
  uint64_t max_;
  std::atomic<int64_t> current_;
};

class ProactiveResource {
public:
  ProactiveResource(const std::string& name, ProactiveResourceMonitorPtr monitor,
                    Stats::Scope& stats_scope)
      : name_(name), monitor_(std::move(monitor)),
        failed_updates_counter_(makeCounter(stats_scope, name, "failed_updates")) {}

  bool tryAllocateResource(uint64_t increment) {
    if (monitor_->tryAllocateResource(increment)) {
      return true;
    } else {
      failed_updates_counter_.inc();
      return false;
    }
  }

  bool tryDeallocateResource(uint64_t decrement) {
    if (monitor_->tryDeallocateResource(decrement)) {
      return true;
    } else {
      failed_updates_counter_.inc();
      return false;
    }
  }

  uint64_t currentResourceUsage() { return monitor_->currentResourceUsage(); }

private:
  const std::string name_;
  ProactiveResourceMonitorPtr monitor_;
  Stats::Counter& failed_updates_counter_;

  Stats::Counter& makeCounter(Stats::Scope& scope, absl::string_view a, absl::string_view b) {
    Stats::StatNameManagedStorage stat_name(absl::StrCat("overload.", a, ".", b),
                                            scope.symbolTable());
    return scope.counterFromStatName(stat_name.statName());
  }
};

} // namespace Server
} // namespace Envoy
