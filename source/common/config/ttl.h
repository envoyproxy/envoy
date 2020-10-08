#pragma once

#include <chrono>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"

#include "absl/container/btree_set.h"

namespace Envoy {
namespace Config {

/**
 * Class for managing TTL expiration of xDS resources. TTLs are managed with a single timer that
 * will always be set to the time until the next TTL, ensuring that we have a constant amount of
 * timers per xDS resource type.
 */
class TtlManager {
public:
  TtlManager(std::function<void(const std::vector<std::string>&)> callback,
             Event::Dispatcher& dispatcher, TimeSource& time_source);

  // RAII tracker to simplify managing when we should be running the update callbacks.
  class ScopedTtlUpdate {
  public:
    ~ScopedTtlUpdate() {
      if (--parent_.scoped_update_counter_ == 0) {
        parent_.refreshTimer();
      }
    }

  private:
    ScopedTtlUpdate(TtlManager& parent) : parent_(parent) { parent_.scoped_update_counter_++; }

    friend TtlManager;

    TtlManager& parent_;
  };

  ScopedTtlUpdate scopedTtlUpdate() { return ScopedTtlUpdate(*this); }

  void add(std::chrono::milliseconds ttl, const std::string& name);

  void clear(const std::string& name);

private:
  void refreshTimer();

  using TtlSet = std::set<std::pair<MonotonicTime, std::string>>;
  TtlSet ttls_;
  absl::flat_hash_map<std::string, TtlSet::iterator> ttl_lookup_;

  Event::TimerPtr timer_;
  absl::optional<MonotonicTime> last_scheduled_time_;
  uint8_t scoped_update_counter_{};

  std::function<void(const std::vector<std::string>&)> callback_;
  Event::Dispatcher& dispatcher_;
  TimeSource& time_source_;
};
} // namespace Config
} // namespace Envoy