#pragma once
#include <cstdint>
#include <iostream>
#include <queue>

#include "common/common/assert.h"

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
template <class C> class EdfScheduler {
public:
  // Each time peekAgain is called, it will return the best-effort subsequent
  // pick, popping and reinserting the entry as if it had been picked, and
  // inserting it into the pre-picked queue.
  // The first time peekAgain is called, it will return the
  // first item which will be picked, the second time it is called it will
  // return the second item which will be picked. As picks occur, that window
  // will shrink.
  std::shared_ptr<C> peekAgain(std::function<double(const C&)> calculate_weight) {
    if (hasEntry()) {
      prepick_list_.push_back(std::move(queue_.top().entry_));
      std::shared_ptr<C> ret{prepick_list_.back()};
      add(calculate_weight(*ret), ret);
      queue_.pop();
      return ret;
    }
    return nullptr;
  }

  /**
   * Pick queue entry with closest deadline and adds it back using the weight
   *   from calculate_weight.
   * @return std::shared_ptr<C> to next valid the queue entry if or nullptr if none exists.
   */
  std::shared_ptr<C> pickAndAdd(std::function<double(const C&)> calculate_weight) {
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

  /**
   * Insert entry into queue with a given weight. The deadline will be current_time_ + 1 / weight.
   * @param weight floating point weight.
   * @param entry shared pointer to entry, only a weak reference will be retained.
   */
  void add(double weight, std::shared_ptr<C> entry) {
    ASSERT(weight > 0);
    const double deadline = current_time_ + 1.0 / weight;
    EDF_TRACE("Insertion {} in queue with deadline {} and weight {}.",
              static_cast<const void*>(entry.get()), deadline, weight);
    queue_.push({deadline, order_offset_++, entry});
    ASSERT(queue_.top().deadline_ >= current_time_);
  }

  /**
   * Implements empty() on the internal queue. Does not attempt to discard expired elements.
   * @return bool whether or not the internal queue is empty.
   */
  bool empty() const { return queue_.empty(); }

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
