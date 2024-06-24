#pragma once

#include <chrono>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"

namespace Envoy {
namespace Config {

/**
 * Class for managing TTL expiration of xDS resources. TTLs are managed with a single timer that
 * will always be set to the time until the next TTL, ensuring that we have a constant amount of
 * timers per xDS resource type.
 *
 * We use a combination of two data structures here: a std::set that ensures that we can iterate
 * over the pending TTLs in sorted over, and a map from resource name to the set iterator which
 * allows us to efficiently clear or update TTLs. As iterator stability is required to track the
 * iterators, we use std::set over something like ``absl::btree_set``.
 *
 * As a result of these two data structures, all lookups and modifications can be performed in
 * O(log (number of TTL entries)). This comes at the cost of a higher memory overhead versus just
 * using a single data structure.
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

  ScopedTtlUpdate scopedTtlUpdate() { return {*this}; }

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
