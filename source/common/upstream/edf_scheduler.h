#pragma once
#include <cstdint>
#include <iostream>
#include <list>
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

template <class C> class TimeWheel {
  struct EdfEntry {
    double deadline_;
    // We only hold a weak pointer, since we don't support a remove operator. This allows entries
    // to be lazily unloaded from the queue.
    std::weak_ptr<C> entry_;
  };

public:
  TimeWheel(int wheel_size = 4) : wheel_size_(wheel_size), wheel_(wheel_size) {}
  void dump() {
    FANCY_LOG(trace, "prepick size = {}", prepick_list_.size());
    FANCY_LOG(trace, "wheel size = {}",
              std::reduce(wheel_.begin(), wheel_.end(), 0,
                          [](int acc, const auto& l) { return acc + l.size(); }));
    FANCY_LOG(trace, "distant size = {}", distant_entries_.size());
  }
  void maybeAdvanceCurrent() {
    // There is entry in the current slot regardless the entry is expired or not. Leave it here for
    // the next pick.
    while (offset_ < static_cast<decltype(offset_)>(wheel_.size())) {
      if (!wheel_[offset_].empty()) {
        return;
      }
      // Advance slot by 1.
      ++offset_;
      current_lower_boundary_ += per_slot_range_;
    }
    // All slots are empty. Roll the wheel.
    offset_ = 0;
    spreadDistantEntries(distant_entries_, wheel_, current_lower_boundary_, per_slot_range_);
  }
  void addDeadline(const double& deadline, std::weak_ptr<C> entry) {
    ASSERT(deadline > current_lower_boundary_);
    EDF_TRACE("Insertion {} in queue with deadline {}.", static_cast<const void*>(entry.get()),
              deadline);
    int offset = static_cast<int>((deadline - current_lower_boundary_) / per_slot_range_);
    if (offset + offset_ >= static_cast<int>(wheel_.size())) {
      // add to upper level
      distant_entries_.push_back({deadline, entry});
    } else {
      wheel_[offset + offset_].push_back({deadline, entry});
    }
    maybeAdvanceCurrent();
  }
  void add(const double& weight, std::weak_ptr<C> entry) {
    ASSERT(weight > 0);
    const double deadline = current_lower_boundary_ + 1.0 / weight;
    EDF_TRACE("Insertion {} in queue with deadline {} and weight {}.",
              static_cast<const void*>(entry.get()), deadline, weight);
    addDeadline(deadline, std::move(entry));
  }
  // TODO(lambdai): Currently the code assumes rotate the wheel by 1 is sufficient. It's not always
  // true. Also need to fix with hierarchical wheels.
  bool spreadDistantEntries(std::list<EdfEntry>& upper, std::vector<std::list<EdfEntry>>& lower,
                            const double& base, const double& span) {
    if (upper.empty()) {
      return false;
    }
    int nr_slot = lower.size();
    double upper_bound = base + nr_slot * span;

    for (auto iter = upper.begin(); iter != upper.end();) {
      // Unlikely too large.
      if (iter->deadline_ > upper_bound) {
        ++iter;
        continue;
      }
      // No precision lost before 2 ^ 53 / (number of slot).
      int offset = static_cast<int>((iter->deadline_ - base) / span);
      // Push back to each slot so that the early element in upper wheel is early in lower wheel.
      lower[offset].splice(lower[offset].end(), upper, iter++);
    }
    // FIX-ME
    return true;
  }

  std::shared_ptr<C> pickAndAdd(std::function<double(const C&)> calculate_weight) {
    while (!prepick_list_.empty()) {
      // In this case the entry was added back during peekAgain so don't re-add.
      if (prepick_list_.front().expired()) {
        prepick_list_.pop();
        continue;
      }
      std::shared_ptr<C> ret{prepick_list_.front()};
      prepick_list_.pop();
      return ret;
    }

    while (true) {
      while (offset_ < wheel_size_) {
        auto& current_slot = wheel_[offset_];
        while (!current_slot.empty()) {
          const EdfEntry& edf_entry = current_slot.front();
          // Entry has been removed, let's see if there's another one.
          if (edf_entry.entry_.expired()) {
            EDF_TRACE("Entry has expired, repick.");
            current_slot.pop_front();
            continue;
          }
          std::shared_ptr<C> ret{current_slot.front().entry_};
          add(calculate_weight(*ret), ret);
          current_slot.pop_front();
          maybeAdvanceCurrent();
          return ret;
        }
        ++offset_;
      }
      // Now offset_ == wheel_size_
      bool spreaded =
          spreadDistantEntries(distant_entries_, wheel_, current_lower_boundary_, per_slot_range_);
      if (!spreaded) {
        // FIX-ME: repeatedly spread until distant entries are empty.
        return nullptr;
      }
    }
  }

  std::shared_ptr<C> peekAgain(std::function<double(const C&)> calculate_weight) {
    auto [has_next, next] = nextAvailableInWheel();
    if (has_next) {
      prepick_list_.push(next->entry_);
      const double deadline = next->deadline_ + 1.0 /
                                                    // has_next guarantee the shared_ptr exists.
                                                    calculate_weight(*(next->entry_.lock()));
      addDeadline(deadline, next->entry_);
      wheel_[offset_].pop_front();
      maybeAdvanceCurrent();
      return next->entry_.lock();
    }
    return nullptr;
  }
  // TODO(lambdai): When is empty called? Consider maintain the current non-empty slot in the rest
  // of the flow.
  bool empty() const {
    return distant_entries_.empty() &&
           std::all_of(wheel_.begin(), wheel_.end(), [](auto& slot) { return slot.empty(); });
  }

private:
  std::pair<bool, EdfEntry*> nextAvailableInWheel() {
    // TODO(lambdai): dedup with pickAndAdd
    while (true) {
      while (offset_ < wheel_size_) {
        auto& current_slot = wheel_[offset_];
        while (!current_slot.empty()) {
          EdfEntry& edf_entry = current_slot.front();
          // Entry has been removed, let's see if there's another one.
          if (edf_entry.entry_.expired()) {
            EDF_TRACE("Entry has expired, repick.");
            current_slot.pop_front();
            continue;
          }
          return std::pair<bool, EdfEntry*>(true, &edf_entry);
        }
        ++offset_;
      }
      // Now offset_ == wheel_size_
      bool spreaded =
          spreadDistantEntries(distant_entries_, wheel_, current_lower_boundary_, per_slot_range_);
      if (!spreaded) {
        // FIX-ME: repeatedly spread until distant entries are empty.
        return std::make_pair<bool, EdfEntry*>(false, nullptr);
      }
    }
  }
  const int wheel_size_;
  std::vector<std::list<EdfEntry>> wheel_;
  double current_lower_boundary_{0.0};
  int offset_{0};
  double per_slot_range_{1};
  double current_time_{0.0};
  std::list<EdfEntry> distant_entries_;
  std::queue<std::weak_ptr<C>, std::deque<std::weak_ptr<C>>> prepick_list_;
};

// Earliest Deadline First (EDF) scheduler
// (https://en.wikipedia.org/wiki/Earliest_deadline_first_scheduling) used for weighted round
// robin. Each pick from the schedule has the earliest deadline entry selected. Entries have
// deadlines set at current time + 1 / weight, providing weighted round robin behavior with
// floating point weights and an O(log n) pick time.
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
    // We only hold a weak pointer, since we don't support a remove operator. This allows entries
    // to be lazily unloaded from the queue.
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
  // Offset used during addition to break ties when entries have the same weight but should
  // reflect FIFO insertion order in picks.
  uint64_t order_offset_{};
  // Min priority queue for EDF.
  std::priority_queue<EdfEntry> queue_;
  std::list<std::weak_ptr<C>> prepick_list_;
};

#undef EDF_DEBUG

} // namespace Upstream
} // namespace Envoy
