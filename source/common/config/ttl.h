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
             Event::Dispatcher& dispatcher, TimeSource& time_source)
      : callback_(callback), dispatcher_(dispatcher), time_source_(time_source) {
    timer_ = dispatcher_.createTimer([this]() {
      ScopedTtlUpdate scoped_update(*this);

      std::vector<std::string> expired;
      last_scheduled_time_ = absl::nullopt;

      const auto now = time_source_.monotonicTime();
      auto itr = ttls_.begin();
      while (itr != ttls_.end() && itr->first <= now) {
        expired.push_back(itr->second);
        itr++;
      }

      if (itr != ttls_.begin()) {
        ttls_.erase(ttls_.begin(), itr);
      }

      if (!expired.empty()) {
        callback_(expired);
      }
    });
  }

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

  void add(std::chrono::milliseconds ttl, const std::string& name) {
    ScopedTtlUpdate scoped_update(*this);

    clear(name);

    auto itr_and_inserted = ttls_.insert({time_source_.monotonicTime() + ttl, name});
    ttl_lookup_[name] = itr_and_inserted.first;
  }

  void clear(const std::string& name) {
    ScopedTtlUpdate scoped_update(*this);

    auto lookup_itr = ttl_lookup_.find(name);
    if (lookup_itr != ttl_lookup_.end()) {
      ttls_.erase(lookup_itr->second);
      ttl_lookup_.erase(lookup_itr);
    }
  }

  void refreshTimer() {
    // No TTLs, so just disable the timer.
    if (ttls_.empty()) {
      timer_->disableTimer();
      return;
    }

    auto next_ttl_expiry = ttls_.begin()->first;

    // The currently scheduled timer execution matches the next TTL expiry, so do nothing.
    if (timer_->enabled() && last_scheduled_time_ == next_ttl_expiry) {
      return;
    }

    auto timer_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        next_ttl_expiry - time_source_.monotonicTime());

    // The time until the next TTL changed, so reset the timer to match the new value.
    last_scheduled_time_ = next_ttl_expiry;
    timer_->enableTimer(timer_duration, nullptr);
  }

private:
  using TtlSet = absl::btree_set<std::pair<MonotonicTime, std::string>>;
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