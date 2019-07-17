#pragma once

#include <array>
#include <atomic>
#include <numeric>

#include "envoy/event/dispatcher.h"
#include "envoy/singleton/instance.h"
#include "envoy/upstream/cluster_manager.h"

#include "extensions/filters/network/redis_proxy/redirection_mgr.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

class RedirectionManagerImpl : public RedirectionManager,
                               public Envoy::Singleton::Instance,
                               public std::enable_shared_from_this<RedirectionManagerImpl> {
public:
  friend class RedirectionMgrTest;

  /**
   * This is a thread-safe tracker for keeping track of the number of redirection errors, aka
   * events, for the last minute with a second granularity.
   */
  class EventsPerMinuteTracker {
  public:
    EventsPerMinuteTracker(TimeSource& time_source) : time_source_(time_source) {}
    virtual ~EventsPerMinuteTracker() = default;

    /**
     * Update the event rate with respect to the current time, and possibly record the fact that a
     * new event has occurred.
     * @param record true if a new event has occurred, false otherwise.
     */
    void update(bool record = true) {
      uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                         time_source_.monotonicTime().time_since_epoch())
                         .count();

      uint32_t now_index, now_plus_one_index, last_index, stop_index;
      uint64_t now_count, now_plus_one_count, last_time, expected;

      do {
        last_time = last_tracked_time_s_.load();
        expected = last_time;
        last_index = last_time % 60;

        if (now <= last_time) {
          // This is not the first event for this second. Simply increment the count and leave.
          if (record) {
            event_counts_[last_index].fetch_add(1);
            event_total_.fetch_add(1);
          }
          return;
        }

        now_index = now % 60;
        // Cache the event count for the now bucket for thread-safe history clearing later.
        now_count = event_counts_[now_index].load();

        if (now - last_time >= 60) {
          // Cache the event count for the now + 1s bucket for thread-safe history clearing
          // only in the case in which all buckets must be cleared (stale data).
          now_plus_one_index = laterIndex(now_index);
          now_plus_one_count = event_counts_[now_plus_one_index].load();
        }

      } while (!last_tracked_time_s_.compare_exchange_strong(expected, now));

      // Subtracting rather than storing zero for now_index will preserve the count of any other
      // events that are being tracked for this current second on other threads.
      event_counts_[now_index].fetch_sub(now_count);
      event_total_.fetch_sub(now_count);

      // Clear older history.
      stop_index = (now - last_time < 60) ? last_index : now_index;
      if (now - last_time < 60) {
        for (uint32_t index = earlierIndex(now_index); index != stop_index;
             index = earlierIndex(index)) {
          event_total_.fetch_sub(event_counts_[index].load());
          event_counts_[index].store(0);
        }
      } else {
        // Start clearing history in the next second bucket using the previously retrieved
        // now_plus_one_count to avoid losing track of events that might have spilled into now + 1s.
        event_counts_[now_plus_one_index].fetch_sub(now_plus_one_count);
        event_total_.fetch_sub(now_plus_one_count);

        for (uint32_t index = laterIndex(now_plus_one_index); index != stop_index;
             index = laterIndex(index)) {
          event_total_.fetch_sub(event_counts_[index].load());
          event_counts_[index].store(0);
        }
      }

      // Count this event now.
      if (record) {
        event_counts_[now_index].fetch_add(1);
        event_total_.fetch_add(1);
      }
    }

    /**
     * Update and retrieve the current event rate as the number of events in the last minute.
     * @return uint64_t the number of events in the last minute.
     */
    uint64_t eventsPerMinute() {
      update(false);
      return event_total_.load();
    }

  private:
    uint32_t earlierIndex(const uint32_t index) { return (index == 0) ? 59 : index - 1; }
    uint32_t laterIndex(const uint32_t index) { return (index == 59) ? 0 : index + 1; }

    TimeSource& time_source_;
    std::atomic<uint64_t> last_tracked_time_s_{};
    std::array<std::atomic<uint64_t>, 60> event_counts_{};
    std::atomic<uint64_t> event_total_{};
  };

  using EventsPerMinuteTrackerPtr = std::unique_ptr<EventsPerMinuteTracker>;

  /**
   * The information that the manager keeps for each cluster upon registration.
   */
  struct ClusterInfo {
    ClusterInfo(std::chrono::milliseconds min_time_between_triggering,
                uint32_t redirects_per_minute_threshold, RedirectCB cb, TimeSource& time_source)
        : tracker_(std::make_unique<EventsPerMinuteTracker>(time_source)),
          min_time_between_triggering_(min_time_between_triggering),
          redirects_per_minute_threshold_(redirects_per_minute_threshold), cb_(std::move(cb)) {}
    EventsPerMinuteTrackerPtr tracker_;
    std::atomic<uint64_t> last_callback_time_ms_{};
    std::chrono::milliseconds min_time_between_triggering_;
    uint32_t redirects_per_minute_threshold_;
    RedirectCB cb_;
  };

  using ClusterInfoSharedPtr = std::shared_ptr<ClusterInfo>;

  class HandleImpl : public Handle {
  public:
    HandleImpl(const std::string& cluster_name, RedirectionManagerImpl* mgr)
        : manager_(mgr->shared_from_this()), cluster_name_(cluster_name) {}

    ~HandleImpl() override {
      std::shared_ptr<RedirectionManagerImpl> strong_manager = manager_.lock();
      if (strong_manager) {
        strong_manager->unregisterCluster(cluster_name_);
      }
    }

  private:
    std::weak_ptr<RedirectionManagerImpl> manager_;
    std::string cluster_name_;
  };

  RedirectionManagerImpl(Event::Dispatcher& main_thread_dispatcher, Upstream::ClusterManager& cm,
                         TimeSource& time_source)
      : main_thread_dispatcher_(main_thread_dispatcher), cm_(cm), time_source_(time_source) {}

  bool onRedirection(const std::string& cluster_name) override;

  HandlePtr registerCluster(const std::string& cluster_name,
                            const std::chrono::milliseconds min_time_between_triggering,
                            const uint32_t redirects_per_minute_threshold,
                            const RedirectCB cb) override;

  void unregisterCluster(const std::string& cluster_name) override;

private:
  Event::Dispatcher& main_thread_dispatcher_;
  Upstream::ClusterManager& cm_;
  TimeSource& time_source_;
  std::map<std::string, ClusterInfoSharedPtr> info_map_;
};

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
