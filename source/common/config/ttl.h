#pragma once

#include "envoy/event/timer.h"
#include "envoy/event/dispatcher.h"
#include "absl/container/btree_set.h"
#include <chrono>

namespace Envoy {
namespace Config {
class Ttl {
public:
  Ttl(std::function<void(const std::vector<std::string>&)> callback, Event::Dispatcher& dispatcher, TimeSource& time_source)
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

  void add(std::chrono::milliseconds ttl, const std::string& name) {
    // TODO(snowp): Expose this RAII object so we can add/clear multiple TTL values before adjusting the timer.
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
  // RAII tracker to simplify managing when we should be running the update callbacks.
  struct ScopedTtlUpdate {
    ScopedTtlUpdate(Ttl& parent) : parent_(parent) { parent_.scoped_update_counter_++; }

    ~ScopedTtlUpdate() {
      if (--parent_.scoped_update_counter_ == 0) {
        parent_.refreshTimer();
      }
    }

    Ttl& parent_;
  };

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