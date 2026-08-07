#pragma once

#include <list>
#include <memory>
#include <type_traits>
#include <utility>

#include "source/common/common/assert.h"
#include "source/common/common/linked_object.h"
#include "source/common/queue_policy/queue_policy_base.h"

#include "absl/functional/function_ref.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {

template <class ItemType> class FifoQueue : public QueueBase<ItemType> {
  static_assert(std::is_base_of_v<LinkedObject<ItemType>, ItemType>,
                "FIFO queue item type must inherit from LinkedObject");

public:
  using ItemPtrType = typename QueueBase<ItemType>::ItemPtrType;

  FifoQueue() = default;
  ~FifoQueue() override = default;

  size_t size() const override { return items_.size(); }

  bool empty() const override { return items_.empty(); }

  ConnectionPool::Cancellable* add(ItemPtrType&& item, QueueItemMetadata) override {
    LinkedList::moveIntoListBack(std::move(item), items_);
    return items_.back().get();
  }

  ItemPtrType remove(ItemType& item) override { return item.removeFromList(items_); }

  ItemType& next() const override {
    ASSERT(!items_.empty());
    return *items_.front();
  }

  bool isOverloaded() const override { return false; }

  void forEach(absl::FunctionRef<bool(ItemType&)> cb) override {
    for (auto it = items_.begin(); it != items_.end();) {
      // Advance before invoking the callback so that the callback may safely remove the
      // visited item from the queue.
      ItemType& item = **it;
      ++it;
      if (!cb(item)) {
        return;
      }
    }
  }

private:
  std::list<ItemPtrType> items_;
};

} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
