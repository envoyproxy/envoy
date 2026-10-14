#pragma once

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <utility>

#include "source/common/common/assert.h"
#include "source/common/queue_policy/queue_policy_base.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {

// Tracks the minimum queue delay over fixed intervals and reports whether a dequeued item should
// be dropped. This is the service-time CoDel variant used by server-side request queues.
class CodelController {
public:
  CodelController(std::chrono::milliseconds target_delay, std::chrono::milliseconds interval)
      : target_delay_(target_delay), interval_(interval) {
    ASSERT(target_delay_.count() > 0);
    ASSERT(interval_ > target_delay_);
  }

  bool shouldDrop(MonotonicTime enqueue_time, MonotonicTime now) {
    ASSERT(now >= enqueue_time);
    const MonotonicTime::duration queue_delay = now - enqueue_time;

    if (!interval_end_.has_value()) {
      interval_end_ = now + interval_;
      min_delay_ = queue_delay;
      return false;
    }

    if (now >= interval_end_.value()) {
      overloaded_ = min_delay_ > target_delay_;
      interval_end_ = now + interval_;
      min_delay_ = queue_delay;
      return false;
    }

    min_delay_ = std::min(min_delay_, queue_delay);
    return overloaded_ && queue_delay > 2 * target_delay_;
  }

  bool isOverloaded() const { return overloaded_; }

  void reset() {
    interval_end_.reset();
    min_delay_ = MonotonicTime::duration::zero();
    overloaded_ = false;
  }

private:
  const std::chrono::milliseconds target_delay_;
  const std::chrono::milliseconds interval_;
  std::optional<MonotonicTime> interval_end_;
  MonotonicTime::duration min_delay_{};
  bool overloaded_{false};
};

// Applies CoDel admission control to an underlying queue policy. The wrapped queue determines
// dequeue order; CoDel only decides whether the selected item is dispatched or dropped.
template <class ItemType> class CodelQueue : public QueueBase<ItemType> {
public:
  CodelQueue(QueuePolicyUniquePtr<ItemType> underlying_queue,
             std::chrono::milliseconds target_delay, std::chrono::milliseconds interval)
      : underlying_queue_(std::move(underlying_queue)), controller_(target_delay, interval) {
    ASSERT(underlying_queue_ != nullptr);
  }

  size_t size() const override { return underlying_queue_->size(); }
  bool empty() const override { return underlying_queue_->empty(); }

  void add(ItemType& item, QueueItemMetadata metadata) override {
    ASSERT(enqueue_times_.find(&item) == enqueue_times_.end());
    enqueue_times_.emplace(&item, metadata.enqueue_time_);
    underlying_queue_->add(item, metadata);
  }

  const ItemType& peek() const override { return underlying_queue_->peek(); }
  ItemType& peek() override { return underlying_queue_->peek(); }

  ItemType& pop() override {
    ItemType& item = underlying_queue_->pop();
    enqueue_times_.erase(&item);
    resetIfEmpty();
    return item;
  }

  DequeueAction onDequeue(ItemType& item, MonotonicTime now) override {
    const DequeueAction underlying_action = underlying_queue_->onDequeue(item, now);
    if (underlying_action == DequeueAction::Drop) {
      return underlying_action;
    }

    const auto enqueue_time = enqueue_times_.find(&item);
    ASSERT(enqueue_time != enqueue_times_.end());
    return controller_.shouldDrop(enqueue_time->second, now) ? DequeueAction::Drop
                                                             : DequeueAction::Dispatch;
  }

  void remove(ItemType& item) override {
    underlying_queue_->remove(item);
    enqueue_times_.erase(&item);
    resetIfEmpty();
  }

  bool isOverloaded() const override {
    return underlying_queue_->isOverloaded() || controller_.isOverloaded();
  }

  void forEach(absl::FunctionRef<bool(ItemType&)> cb) override { underlying_queue_->forEach(cb); }

private:
  void resetIfEmpty() {
    if (underlying_queue_->empty()) {
      controller_.reset();
    }
  }

  const QueuePolicyUniquePtr<ItemType> underlying_queue_;
  CodelController controller_;
  absl::flat_hash_map<ItemType*, MonotonicTime> enqueue_times_;
};

} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
