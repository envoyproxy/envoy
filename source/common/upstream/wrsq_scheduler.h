#pragma once

#include <algorithm>
#include <iostream>
#include <memory>
#include <queue>
#include <utility>
#include <vector>

#include "envoy/common/random_generator.h"
#include "envoy/upstream/scheduler.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Upstream {

// Weighted Random Selection Queue (WRSQ) Scheduler
// ------------------------------------------------
// This scheduler keeps a queue for each unique weight among all objects inserted and adds the
// objects to their respective queue based on weight. When performing a pick operation, a queue is
// selected and an object is pulled. Each queue gets its own selection probability which is weighted
// as the sum of all weights of objects contained within. Once a queue is picked, you can simply
// pull from the top and honor the expected selection probability of each object.
//
// Adding an object will cause the scheduler to rebuild internal structures on the first pick that
// follows. This first pick operation will be linear on the number of unique weights among objects
// inserted. Subsequent picks will be logarithmic with the number of unique weights. Adding objects
// is always constant time.
//
// For the case where all object weights are the same, WRSQ behaves identical to vanilla
// round-robin. If all object weights are different, it behaves identical to weighted random
// selection.
//
// NOTE: While the base scheduler interface allows for mutation of object weights with each pick,
// this implementation is not meant for circumstances where the object weights change with each pick
// (like in the least request LB). This scheduler implementation will perform quite poorly if the
// object weights change often.
template <class C>
class WRSQScheduler : public Scheduler<C>, protected Logger::Loggable<Logger::Id::upstream> {
public:
  WRSQScheduler(Random::RandomGenerator& random) : random_(random) {}

  std::shared_ptr<C> peekAgain(std::function<double(const C&)> calculate_weight) override {
    std::shared_ptr<C> picked{pickAndAddInternal(calculate_weight)};
    if (picked != nullptr) {
      prepick_queue_.emplace(picked);
    }
    return picked;
  }

  std::shared_ptr<C> pickAndAdd(std::function<double(const C&)> calculate_weight) override {
    // Burn through the pre-pick queue.
    while (!prepick_queue_.empty()) {
      std::shared_ptr<C> prepicked_obj = prepick_queue_.front().lock();
      prepick_queue_.pop();
      if (prepicked_obj != nullptr) {
        return prepicked_obj;
      }
    }

    return pickAndAddInternal(calculate_weight);
  }

  void add(double weight, std::shared_ptr<C> entry) override {
    rebuild_cumulative_weights_ = true;
    queue_map_[weight].emplace(std::move(entry));
  }

  bool empty() const override { return queue_map_.empty(); }

private:
  using ObjQueue = std::queue<std::weak_ptr<C>>;

  // TODO(tonya11en): We can reduce memory utilization by using an absl::flat_hash_map of QueueInfo
  // with heterogeneous lookup on the weight. This would allow us to save 8 bytes per unique weight.
  using QueueMap = absl::flat_hash_map<double, ObjQueue>;

  // Used to store a queue's weight info necessary to perform the weighted random selection.
  struct QueueInfo {
    double cumulative_weight;
    double weight;
    ObjQueue& q;
  };

  // If needed, such as after object expiry or addition, rebuild the cumulative weights vector.
  void maybeRebuildCumulativeWeights() {
    if (!rebuild_cumulative_weights_) {
      return;
    }

    cumulative_weights_.clear();
    cumulative_weights_.reserve(queue_map_.size());

    double weight_sum = 0;
    for (auto& it : queue_map_) {
      const auto weight_val = it.first;
      weight_sum += weight_val * it.second.size();
      cumulative_weights_.push_back({weight_sum, weight_val, it.second});
    }

    rebuild_cumulative_weights_ = false;
  }

  // Performs a weighted random selection on the queues containing objects of the same weight.
  // Popping off the top of the queue to pick an object will honor the selection probability based
  // on the weight provided when the object was added.
  QueueInfo& chooseQueue() {
    ASSERT(!queue_map_.empty());

    maybeRebuildCumulativeWeights();

    const double weight_sum = cumulative_weights_.back().cumulative_weight;
    uint64_t rnum = random_.random() % static_cast<uint32_t>(weight_sum);
    auto it = std::upper_bound(cumulative_weights_.begin(), cumulative_weights_.end(), rnum,
                               [](auto a, auto b) { return a < b.cumulative_weight; });
    ASSERT(it != cumulative_weights_.end());
    return *it;
  }

  // Remove objects from the queue until it's empty or there is an unexpired object at the front. If
  // the queue is purged to empty, it's removed from the queue map and we return true.
  bool purgeExpired(QueueInfo& qinfo) {
    while (!qinfo.q.empty() && qinfo.q.front().expired()) {
      qinfo.q.pop();
      rebuild_cumulative_weights_ = true;
    }

    if (qinfo.q.empty()) {
      queue_map_.erase(qinfo.weight);
      return true;
    }
    return false;
  }

  std::shared_ptr<C> pickAndAddInternal(std::function<double(const C&)> calculate_weight) {
    while (!queue_map_.empty()) {
      QueueInfo& qinfo = chooseQueue();
      if (purgeExpired(qinfo)) {
        // The chosen queue was purged to empty and removed from the queue map. Try again.
        continue;
      }

      auto obj = qinfo.q.front().lock();
      qinfo.q.pop();
      if (obj == nullptr) {
        // The object expired after the purge.
        continue;
      }

      const double new_weight = calculate_weight ? calculate_weight(*obj) : qinfo.weight;
      if (new_weight == qinfo.weight) {
        qinfo.q.emplace(obj);
      } else {
        // The weight has changed for this object, so we must re-add it to the scheduler.
        ENVOY_LOG_EVERY_POW_2(
            warn, "WRSQ scheduler is used with a load balancer that mutates host weights with each "
                  "selection, this will likely result in poor selection performance");
        add(new_weight, obj);
      }

      return obj;
    }

    return nullptr;
  }

  Random::RandomGenerator& random_;

  // Objects already picked via peekAgain().
  ObjQueue prepick_queue_;

  // A mapping from an object weight to the associated queue.
  QueueMap queue_map_;

  // Stores the necessary information to perform a weighted random selection of the different
  // queues. A cumulative sum is also kept of the total object weights for a queue, which allows for
  // a single random number generation and a binary search to pick a queue.
  std::vector<QueueInfo> cumulative_weights_;

  // Keeps state that determines whether the cumulative weights need to be rebuilt. If any objects
  // contained in a queue change from addition or expiry, it throws off the cumulative weight
  // values. Therefore, they must be recalculated.
  bool rebuild_cumulative_weights_{true};
};

} // namespace Upstream
} // namespace Envoy
