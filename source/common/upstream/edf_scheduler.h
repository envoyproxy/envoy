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
  EdfScheduler() = default;

  // See scheduler.h for an explanation of each public method.
  std::shared_ptr<C> peekAgain(std::function<double(const C&)> calculate_weight) override {
    std::shared_ptr<C> ret = popEntry();
    if (ret) {
      prepick_list_.push_back(ret);
      add(calculate_weight(*ret), ret);
    }
    return ret;
  }

  std::shared_ptr<C> pickAndAdd(std::function<double(const C&)> calculate_weight) override {
    while (!prepick_list_.empty()) {
      // In this case the entry was added back during peekAgain so don't re-add.
      std::shared_ptr<C> ret = prepick_list_.front().lock();
      prepick_list_.pop_front();
      if (ret) {
        return ret;
      }
    }
    std::shared_ptr<C> ret = popEntry();
    if (ret) {
      add(calculate_weight(*ret), ret);
    }
    return ret;
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

  // Creates an EdfScheduler with the given weights and their corresponding
  // entries, and emulating a number of initial picks to be performed. Note that
  // the internal state of the scheduler will be very similar to creating an empty
  // scheduler, adding the entries one after the other, and then performing
  // "picks" pickAndAdd operation without modifying the entries' weights.
  // The only thing that may be different is that entries with the same weight
  // may be chosen a bit differently (the order_offset_ values may be different).
  // Breaking the ties of same weight entries will be kept in future picks from
  // the scheduler.
  static EdfScheduler<C> createWithPicks(const std::vector<std::shared_ptr<C>>& entries,
                                         std::function<double(const C&)> calculate_weight,
                                         uint32_t picks) {
    // Limiting the number of picks, as over 400M picks should be sufficient
    // for most scenarios.
    picks = picks % 429496729; // % UINT_MAX/10
    EDF_TRACE("Creating an EDF-scheduler with {} weights and {} pre-picks.", entries.size(), picks);
    // Assume no non-positive weights.
    ASSERT(std::none_of(entries.cbegin(), entries.cend(),
                        [&calculate_weight](const std::shared_ptr<C>& entry) {
                          return calculate_weight(*entry) <= 0;
                        }));

    // Nothing to do if there are no entries.
    if (entries.empty()) {
      return EdfScheduler<C>();
    }

    // Augment the weight computation to add some epsilon to each entry's
    // weight to avoid cases where weights are multiplies of each other. For
    // example if there are 2 weights: 25 and 75, and picks=23, then the
    // floor_picks will be {5, 17} (respectively), and the deadlines will be
    // {0.24000000000000002 and 0.24} (respectively). This small difference will
    // cause a "wrong" pick compared to when starting from an empty scheduler
    // and picking 23 times. Adding a small value to each weight circumvents
    // this problem. This was added as a result of the following comment:
    // https://github.com/envoyproxy/envoy/pull/31592#issuecomment-1877663769.
    auto aug_calculate_weight = [&calculate_weight](const C& entry) -> double {
      return calculate_weight(entry) + 1e-13;
    };

    // Let weights {w_1, w_2, ..., w_N} be the per-entry weight where (w_i > 0),
    // W = sum(w_i), and P be the number of times to "pick" from the scheduler.
    // Let p'_i = floor(P * w_i/W), then the number of times each entry is being
    // picked is p_i >= p'_i. Note that 0 <= P - sum(p'_i) < N.
    //
    // The following code does P picks, by first emulating p'_i picks for each
    // entry, and then executing the leftover P - sum(p'_i) picks.
    double weights_sum = std::accumulate(
        entries.cbegin(), entries.cend(), 0.0,
        [&aug_calculate_weight](double sum_so_far, const std::shared_ptr<C>& entry) {
          return sum_so_far + aug_calculate_weight(*entry);
        });
    std::vector<uint32_t> floor_picks;
    floor_picks.reserve(entries.size());
    std::transform(entries.cbegin(), entries.cend(), std::back_inserter(floor_picks),
                   [picks, weights_sum, &aug_calculate_weight](const std::shared_ptr<C>& entry) {
                     // Getting the lower-bound by casting to an integer.
                     return static_cast<uint32_t>(aug_calculate_weight(*entry) * picks /
                                                  weights_sum);
                   });

    // Pre-compute the priority-queue entries to use an O(N) initialization c'tor.
    std::vector<EdfEntry> scheduler_entries;
    scheduler_entries.reserve(entries.size());
    uint32_t picks_so_far = 0;
    double max_pick_time = 0.0;
    // Emulate a per-entry addition to a deadline that is applicable to N picks.
    for (size_t i = 0; i < entries.size(); ++i) {
      // Add the entry with p'_i picks. As there were p'_i picks, the entry's
      // next deadline is (p'_i + 1) / w_i.
      const double weight = aug_calculate_weight(*entries[i]);
      // While validating the algorithm there were a few cases where the math
      // and floating-point arithmetic did not agree (specifically floor(A*B)
      // was greater than A*B). The following if statement solves the problem by
      // reducing floor-picks for the entry, which may result in more iterations
      // in the code after the loop.
      if ((floor_picks[i] > 0) && (floor_picks[i] / weight >= picks / weights_sum)) {
        floor_picks[i]--;
      }
      const double pick_time = floor_picks[i] / weight;
      const double deadline = (floor_picks[i] + 1) / weight;
      EDF_TRACE("Insertion {} in queue with emualted {} picks, deadline {} and weight {}.",
                static_cast<const void*>(entries[i].get()), floor_picks[i], deadline, weight);
      scheduler_entries.emplace_back(EdfEntry{deadline, i, entries[i]});
      max_pick_time = std::max(max_pick_time, pick_time);
      picks_so_far += floor_picks[i];
    }
    // The scheduler's current_time_ needs to be the largest time that some entry was picked.
    EdfScheduler<C> scheduler(std::move(scheduler_entries), max_pick_time, entries.size());
    ASSERT(scheduler.queue_.top().deadline_ >= scheduler.current_time_);

    // Left to do some picks, execute them one after the other.
    EDF_TRACE("Emulated {} picks in init step, {} picks remaining for one after the other step",
              picks_so_far, picks - picks_so_far);
    while (picks_so_far < picks) {
      scheduler.pickAndAdd(calculate_weight);
      picks_so_far++;
    }
    return scheduler;
  }

private:
  friend class EdfSchedulerTest;

  /**
   * Clears expired entries and pops the next unexpired entry in the queue.
   */
  std::shared_ptr<C> popEntry() {
    EDF_TRACE("Queue pick: queue_.size()={}, current_time_={}.", queue_.size(), current_time_);
    while (true) {
      if (queue_.empty()) {
        EDF_TRACE("Queue is empty.");
        return nullptr;
      }
      const EdfEntry& edf_entry = queue_.top();
      // Entry has been removed, let's see if there's another one.
      std::shared_ptr<C> ret = edf_entry.entry_.lock();
      if (!ret) {
        EDF_TRACE("Entry has expired, repick.");
        queue_.pop();
        continue;
      }
      ASSERT(edf_entry.deadline_ >= current_time_);
      current_time_ = edf_entry.deadline_;
      EDF_TRACE("Picked {}, current_time_={}.", static_cast<const void*>(ret.get()), current_time_);
      queue_.pop();
      return ret;
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

  EdfScheduler(std::vector<EdfEntry>&& scheduler_entries, double current_time,
               uint32_t order_offset)
      : current_time_(current_time), order_offset_(order_offset),
        queue_(scheduler_entries.cbegin(), scheduler_entries.cend()) {}

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
