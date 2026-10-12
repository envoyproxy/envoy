#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <list>
#include <utility>

#include "source/common/common/assert.h"
#include "source/common/queue_policy/queue_policy_base.h"

#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {

template <class ItemType> class AdaptiveLifoQueue : public QueueBase<ItemType> {
public:
  AdaptiveLifoQueue() = default;
  AdaptiveLifoQueue(double high_watermark_percent, double low_watermark_percent,
                    MaxQueueSizeFn max_queue_size)
      : high_watermark_percent_(high_watermark_percent),
        low_watermark_percent_(low_watermark_percent), max_queue_size_(std::move(max_queue_size)) {
    ASSERT(high_watermark_percent > 0 && high_watermark_percent <= 100);
    ASSERT(low_watermark_percent >= 0 && low_watermark_percent < high_watermark_percent);
    ASSERT(static_cast<bool>(max_queue_size_));
    updateWatermarks();
  }
  ~AdaptiveLifoQueue() override = default;

  size_t size() const override { return items_.size(); }

  bool empty() const override { return items_.empty(); }

  void add(ItemType& item, QueueItemMetadata) override {
    ASSERT(item_index_.find(&item) == item_index_.end());
    item_index_.emplace(&item, items_.insert(items_.end(), &item));
    updateMode();
  }

  const ItemType& peek() const override { return *nextItem(); }

  ItemType& peek() override { return *nextItem(); }

  ItemType& pop() override {
    ItemType& item = *nextItem();
    remove(item);
    return item;
  }

  void remove(ItemType& item) override {
    auto entry = item_index_.find(&item);
    ASSERT(entry != item_index_.end());
    items_.erase(entry->second);
    item_index_.erase(entry);
    updateMode();
  }

  bool isOverloaded() const override {
    updateMode();
    return lifo_mode_;
  }

  void forEach(absl::FunctionRef<bool(ItemType&)> cb) override {
    updateMode();
    if (!lifo_mode_) {
      for (auto it = items_.begin(); it != items_.end();) {
        ItemType& item = **it;
        ++it;
        if (!cb(item)) {
          return;
        }
      }
      return;
    }

    auto current = items_.end();
    --current;
    while (true) {
      const bool is_first = current == items_.begin();
      auto next = current;
      if (!is_first) {
        --next;
      }
      ItemType& item = **current;
      if (!cb(item) || is_first) {
        return;
      }
      current = next;
    }
  }

private:
  ItemType* nextItem() const {
    ASSERT(!items_.empty());
    updateMode();
    return lifo_mode_ ? items_.back() : items_.front();
  }

  void updateMode() const {
    if (!max_queue_size_) {
      return;
    }

    updateWatermarks();

    if (lifo_mode_) {
      if (items_.size() <= low_watermark_) {
        lifo_mode_ = false;
      }
    } else if (items_.size() >= high_watermark_) {
      lifo_mode_ = true;
    }
  }

  void updateWatermarks() const {
    const uint64_t max_queue_size = max_queue_size_();
    if (watermarks_initialized_ && max_queue_size == cached_max_queue_size_) {
      return;
    }

    cached_max_queue_size_ = max_queue_size;
    high_watermark_ = std::max<uint64_t>(
        1, static_cast<uint64_t>(std::ceil(static_cast<long double>(max_queue_size) *
                                           high_watermark_percent_ / 100.0L)));
    low_watermark_ = static_cast<uint64_t>(
        std::floor(static_cast<long double>(max_queue_size) * low_watermark_percent_ / 100.0L));
    watermarks_initialized_ = true;
  }

  using ItemList = std::list<ItemType*>;
  const double high_watermark_percent_{0};
  const double low_watermark_percent_{0};
  const MaxQueueSizeFn max_queue_size_;
  ItemList items_;
  absl::flat_hash_map<ItemType*, typename ItemList::iterator> item_index_;
  mutable uint64_t cached_max_queue_size_{0};
  mutable uint64_t high_watermark_{1};
  mutable uint64_t low_watermark_{0};
  mutable bool watermarks_initialized_{false};
  mutable bool lifo_mode_{false};
};

} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
