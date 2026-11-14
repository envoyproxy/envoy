#pragma once

#include <algorithm>
#include <chrono>
#include <list>
#include <memory>
#include <optional>
#include <utility>

#include "source/common/common/assert.h"
#include "source/common/queue_policy/queue_policy_base.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {

// Tracks the minimum queue delay over fixed intervals and reports whether the oldest item should
// be dropped. This is the service-time CoDel variant used by server-side request queues.
class CodelController {
public:
  CodelController(std::chrono::nanoseconds target_delay, std::chrono::nanoseconds interval)
      : target_delay_(target_delay), interval_(interval) {
    ASSERT(target_delay_.count() > 0);
    ASSERT(interval_ > target_delay_);
  }

  bool shouldDrop(MonotonicTime enqueue_time, MonotonicTime now) {
    ASSERT(now >= enqueue_time);
    const MonotonicTime::duration queue_delay = now - enqueue_time;

    if (!interval_start_.has_value()) {
      interval_start_ = now;
      min_delay_ = queue_delay;
      return false;
    }

    if (now - interval_start_.value() >= interval_) {
      overloaded_ = min_delay_ > target_delay_;
      interval_start_ = now;
      min_delay_ = queue_delay;
      return false;
    }

    min_delay_ = std::min(min_delay_, queue_delay);
    // Subtract to avoid overflow when comparing against twice the target.
    return overloaded_ && queue_delay > target_delay_ &&
           queue_delay - target_delay_ > target_delay_;
  }

  bool isOverloaded() const { return overloaded_; }

  void reset() {
    interval_start_.reset();
    min_delay_ = MonotonicTime::duration::zero();
    overloaded_ = false;
  }

private:
  const std::chrono::nanoseconds target_delay_;
  const std::chrono::nanoseconds interval_;
  std::optional<MonotonicTime> interval_start_;
  MonotonicTime::duration min_delay_{};
  bool overloaded_{false};
};

// Applies CoDel load shedding to an underlying queue policy. CoDel measures and drops the oldest
// item independently of the wrapped queue's dispatch order.
template <class ItemType> class CodelQueue : public QueueBase<ItemType> {
public:
  CodelQueue(QueuePolicyUniquePtr<ItemType> underlying_queue, std::chrono::nanoseconds target_delay,
             std::chrono::nanoseconds interval)
      : underlying_queue_(std::move(underlying_queue)), controller_(target_delay, interval) {
    ASSERT(underlying_queue_ != nullptr);
  }

  size_t size() const override { return underlying_queue_->size(); }
  bool empty() const override { return underlying_queue_->empty(); }

  void add(ItemType& item, QueueItemMetadata metadata) override {
    ASSERT(item_index_.find(&item) == item_index_.end());
    item_index_.emplace(
        &item, enqueue_order_.insert(enqueue_order_.end(), {&item, metadata.enqueue_time_}));
    underlying_queue_->add(item, metadata);
  }

  const ItemType& peek() const override { return underlying_queue_->peek(); }
  ItemType& peek() override { return underlying_queue_->peek(); }

  ItemType& pop() override {
    ItemType& item = underlying_queue_->pop();
    eraseEntry(item);
    resetIfEmpty();
    return item;
  }

  ItemType* peekToDrop(MonotonicTime now) override {
    ASSERT(!empty());
    const auto& oldest = enqueue_order_.front();
    if (controller_.shouldDrop(oldest.enqueue_time_, now)) {
      return oldest.item_;
    }
    return underlying_queue_->peekToDrop(now);
  }

  void remove(ItemType& item) override {
    underlying_queue_->remove(item);
    eraseEntry(item);
    resetIfEmpty();
  }

  bool isOverloaded() const override {
    return underlying_queue_->isOverloaded() || controller_.isOverloaded();
  }

  void forEach(absl::FunctionRef<bool(ItemType&)> cb) override { underlying_queue_->forEach(cb); }

private:
  void eraseEntry(ItemType& item) {
    const auto entry = item_index_.find(&item);
    ASSERT(entry != item_index_.end());
    enqueue_order_.erase(entry->second);
    item_index_.erase(entry);
  }

  void resetIfEmpty() {
    if (underlying_queue_->empty()) {
      controller_.reset();
    }
  }

  const QueuePolicyUniquePtr<ItemType> underlying_queue_;
  CodelController controller_;
  struct TimedItem {
    ItemType* item_;
    MonotonicTime enqueue_time_;
  };
  using ItemList = std::list<TimedItem>;
  ItemList enqueue_order_;
  absl::flat_hash_map<ItemType*, typename ItemList::iterator> item_index_;
};

} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
