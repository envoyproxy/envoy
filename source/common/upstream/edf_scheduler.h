#include <cstdint>
#include <queue>

#include "common/common/assert.h"

namespace Envoy {
namespace Upstream {

// It's not sufficient to use trace level logging, since it becomes far too noisy for a number of
// tests, so we can kill EDF trace debug here.
#define EDF_DEBUG 0

#if EDF_DEBUG
#define EDF_TRACE(fmt...) ENVOY_LOG_MISC(trace, fmt)
#else
#define EDF_TRACE(fmt...)
#endif

// EDF scheduler. Entries have deadlines set at current time + 1 / weight, providing weighted round
// robin behavior.
template <class C> class EdfScheduler {
public:
  /**
   * Pick queue entry with closest deadline.
   * @return std::shared_ptr<C> to the queue entry if a valid entry exists in the queue, nullptr
   *         otherwise. The entry is removed from the queue.
   */
  std::shared_ptr<C> pick() {
    EDF_TRACE("Queue pick: queue_.size()={}, current_time_={}.", queue_.size(), current_time_);
    while (true) {
      if (queue_.empty()) {
        EDF_TRACE("Queue is empty.");
        return nullptr;
      }
      const EdfEntry& edf_entry = queue_.top();
      // Entry has been removed, let's see if there's another one.
      if (edf_entry.entry_.expired()) {
        EDF_TRACE("Entry has expired, repick.");
        queue_.pop();
        continue;
      }
      std::shared_ptr<C> ret{edf_entry.entry_};
      ASSERT(edf_entry.deadline_ >= current_time_);
      current_time_ = edf_entry.deadline_;
      order_offset_ = 0;
      queue_.pop();
      EDF_TRACE("Picked {}, current_time_={}.", static_cast<const void*>(ret.get()), current_time_);
      return ret;
    }
  }

  /**
   * Insert entry into queue with a given weight. The deadline will be current_time_ + 1 / weight.
   * @param weight integer weight.
   * @param entry shared pointer to entry, only a weak reference will be retained.
   */
  void add(uint64_t weight, std::shared_ptr<C> entry) {
    ASSERT(weight > 0);
    const double deadline = current_time_ + order_offset_ + 1.0 / weight;
    EDF_TRACE("Insertion {} in queue with deadline {} and weight {}.",
              static_cast<const void*>(entry.get()), deadline, weight);
    queue_.push({deadline, entry});
    // Increment offset to ensure we get deterministic ordering. We need this bias to be small
    // enough to avoid conflicting with other entries in the queue.
    // TODO(htuch): this is a pretty unreliable use of floating point, probably better
    // to switch to integer weights. Using std::numeric_limits<double>::epsilon() show that this
    // doesn't work at the smallest precision in edf_scheduler_test.
    order_offset_ += 0.0000000001;
    ASSERT(queue_.top().deadline_ >= current_time_);
  }

private:
  struct EdfEntry {
    double deadline_;
    // We only hold a weak pointer, since we don't support a remove operator. This allows entries to
    // be lazily unloaded from the queue.
    std::weak_ptr<C> entry_;

    // Flip < direction to make this a min queue.
    bool operator<(const EdfEntry& other) const { return deadline_ > other.deadline_; }
  };

  // Current time in EDF scheduler.
  // TOOD(htuch): Is it worth the small extra complexity to use integer time for performance
  // reasons? Also, there might be some inaccuracy around the use of epsilon with double that could
  // be avoided with integer time.
  double current_time_{};
  // Offset used during addition to break ties when entries have the same weight but should reflect
  // FIFO insertion order in picks.
  double order_offset_{};
  // Min priority queue for EDF.
  std::priority_queue<EdfEntry> queue_;
};

#undef EDF_DEBUG

} // namespace Upstream
} // namespace Envoy
