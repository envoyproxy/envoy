#pragma once
#include <cstdint>
#include <iostream>
#include <queue>

#include "envoy/upstream/scheduler.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Upstream {

// It's not sufficient to use trace level logging, since it becomes far too noisy for a number of
// tests, so we can kill trace debug here.
#define EDF_DEBUG 0

#if EDF_DEBUG
#define EDF_TRACE(...) ENVOY_LOG_MISC(trace, __VA_ARGS__)
#else
#define EDF_TRACE(...)
#endif

// Earliest Deadline First (EDF) scheduler
// (https://en.wikipedia.org/wiki/Earliest_deadline_first_scheduling) used for weighted round robin.
// Each pick from the schedule has the earliest deadline entry selected. Entries have deadlines set
// at current time + 1 / weight, providing weighted round robin behavior with floating point
// weights and an O(log n) pick time.
template <class C> class EdfScheduler : public Scheduler<C> {
public:
  // See scheduler.h for an explanation of each public method.
  std::shared_ptr<C> peekAgain(std::function<double(const C&)> calculate_weight) override {
    if (hasEntry()) {
      prepick_list_.push_back(std::move(queue_.top().entry_));
      std::shared_ptr<C> ret{prepick_list_.back()};
      add(calculate_weight(*ret), ret);
      queue_.pop();
      return ret;
    }
    return nullptr;
  }

  std::shared_ptr<C> pickAndAdd(std::function<double(const C&)> calculate_weight) override {
    while (!prepick_list_.empty()) {
      // In this case the entry was added back during peekAgain so don't re-add.
      if (prepick_list_.front().expired()) {
        prepick_list_.pop_front();
        continue;
      }
      std::shared_ptr<C> ret{prepick_list_.front()};
      prepick_list_.pop_front();
      return ret;
    }
    if (hasEntry()) {
      std::shared_ptr<C> ret{queue_.top().entry_};
      queue_.pop();
      add(calculate_weight(*ret), ret);
      return ret;
    }
    return nullptr;
  }

  void add(double weight, std::shared_ptr<C> entry) override {
    ASSERT(weight > 0);
    const double deadline = current_time_ + 1.0 / weight;
    EDF_TRACE("Insertion {} in queue with deadline {} and weight {}.",
              static_cast<const void*>(entry.get()), deadline, weight);
    queue_.push({deadline, order_offset_++, entry});
    ASSERT(queue_.top().deadline_ >= current_time_);
  }

  bool empty() const override { return queue_.empty(); }

private:
  /**
   * Clears expired entries, and returns true if there's still entries in the queue.
   */
  bool hasEntry() {
    EDF_TRACE("Queue pick: queue_.size()={}, current_time_={}.", queue_.size(), current_time_);
    while (true) {
      if (queue_.empty()) {
        EDF_TRACE("Queue is empty.");
        return false;
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
      EDF_TRACE("Picked {}, current_time_={}.", static_cast<const void*>(ret.get()), current_time_);
      return true;
    }
  }

  struct EdfEntry {
    double deadline_;
    // Tie breaker for entries with the same deadline. This is used to provide FIFO behavior.
    uint64_t order_offset_;
    // We only hold a weak pointer, since we don't support a remove operator. This allows entries to
    // be lazily unloaded from the queue.
    std::weak_ptr<C> entry_;

    // Flip < direction to make this a min queue.
    bool operator<(const EdfEntry& other) const {
      return deadline_ > other.deadline_ ||
             (deadline_ == other.deadline_ && order_offset_ > other.order_offset_);
    }
  };

  // Current time in EDF scheduler.
  // TODO(htuch): Is it worth the small extra complexity to use integer time for performance
  // reasons?
  double current_time_{};
  // Offset used during addition to break ties when entries have the same weight but should reflect
  // FIFO insertion order in picks.
  uint64_t order_offset_{};
  // Min priority queue for EDF.
  std::priority_queue<EdfEntry> queue_;
  std::list<std::weak_ptr<C>> prepick_list_;
};

#undef EDF_DEBUG

} // namespace Upstream
} // namespace Envoy
