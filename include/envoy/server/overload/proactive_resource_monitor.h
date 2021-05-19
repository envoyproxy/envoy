#pragma once

#include <string>

#include "envoy/common/pure.h"

#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

namespace Envoy {
namespace Server {

class ReactiveResourceMonitor {
public:
  ReactiveResourceMonitor() = default;
  virtual ~ReactiveResourceMonitor() = default;
  virtual bool tryAllocateResource(uint64_t increment) PURE;
  virtual bool tryDeallocateResource(uint64_t decrement) PURE;
  virtual uint64_t currentResourceUsage() const PURE;
  virtual uint64_t maxResourceUsage() const PURE;
};

using ReactiveResourceMonitorPtr = std::unique_ptr<ReactiveResourceMonitor>;

// Example of reactive resource monitor. To be removed.
class ActiveConnectionsResourceMonitor : public ReactiveResourceMonitor {
public:
  ActiveConnectionsResourceMonitor(uint64_t max_active_conns)
      : max_(max_active_conns), current_(0){};

  bool tryAllocateResource(uint64_t increment) {
    uint64_t new_val = (current_ += increment);
    if (new_val >= max_) {
      current_ -= increment;
      return false;
    }
    return true;
  }

  bool tryDeallocateResource(uint64_t decrement) {
    ASSERT(decrement <= current_.load());
    // Guard against race condition.
    if (decrement <= current_.load()) {
      current_ -= decrement;
      return true;
    }
    return false;
  }

  uint64_t currentResourceUsage() const { return current_.load(); }
  uint64_t maxResourceUsage() const { return max_; };

protected:
  uint64_t max_;
  std::atomic<uint64_t> current_;
};

  class ReactiveResource {
  public:
    ReactiveResource(const std::string& name, ReactiveResourceMonitorPtr monitor,
                     Stats::Scope& stats_scope);

    bool tryAllocateResource(uint64_t increment);
    bool tryDeallocateResource(uint64_t decrement);
    uint64_t currentResourceUsage();

  private:
    const std::string name_;
    ReactiveResourceMonitorPtr monitor_;
    Stats::Counter& failed_updates_counter_;
  };

} // namespace Server
} // namespace Envoy
