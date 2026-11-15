#pragma once

#include <list>

#include "source/common/common/assert.h"
#include "source/common/queue_policy/queue_policy_base.h"

#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {

// Shared list storage for queue policies that select from either end of the list.
template <class ItemType> class ListQueueBase : public QueueBase<ItemType> {
public:
  size_t size() const override { return items_.size(); }

  bool empty() const override { return items_.empty(); }

  void add(ItemType& item, QueueItemMetadata) override {
    ASSERT(item_index_.find(&item) == item_index_.end());
    item_index_.emplace(&item, items_.insert(items_.end(), &item));
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
  }

  void forEach(absl::FunctionRef<bool(ItemType&)> cb) override {
    if (items_.empty()) {
      return;
    }

    size_t remaining = items_.size();
    ItemType* next_fifo = items_.front();
    ItemType* next_lifo = items_.back();
    while (remaining > 0) {
      const bool use_lifo = shouldDequeueFromBack();
      ItemType* item = use_lifo ? next_lifo : next_fifo;
      auto entry = item_index_.find(item);
      ASSERT(entry != item_index_.end());

      --remaining;
      if (remaining > 0) {
        auto next = entry->second;
        if (use_lifo) {
          ASSERT(next != items_.begin());
          --next;
          next_lifo = *next;
        } else {
          ++next;
          ASSERT(next != items_.end());
          next_fifo = *next;
        }
      }

      if (!cb(*item)) {
        return;
      }
    }
  }

protected:
  // Re-evaluated during traversal because callback removal can change the dequeue order.
  virtual bool shouldDequeueFromBack() const PURE;

private:
  ItemType* nextItem() const {
    ASSERT(!items_.empty());
    return shouldDequeueFromBack() ? items_.back() : items_.front();
  }

  using ItemList = std::list<ItemType*>;
  ItemList items_;
  absl::flat_hash_map<ItemType*, typename ItemList::iterator> item_index_;
};

} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
