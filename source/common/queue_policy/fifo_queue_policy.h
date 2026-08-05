#pragma once

#include <list>
#include <memory>
#include <utility>

#include "source/common/common/assert.h"
#include "source/common/queue_policy/queue_policy_base.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {

template <class ItemType> class FifoQueue : public QueueBase<ItemType> {
public:
  using ItemPtrType = typename QueueBase<ItemType>::ItemPtrType;

  FifoQueue() = default;
  ~FifoQueue() override = default;

  size_t size() const override { return items_.size(); }

  bool empty() const override { return items_.empty(); }

  ConnectionPool::Cancellable* add(ItemPtrType&& item) override {
    LinkedList::moveIntoListBack(std::move(item), items_);
    return items_.back().get();
  }

  ItemPtrType remove(ItemType& item) override { return item.removeFromList(items_); }

  const ItemPtrType& next() const override {
    ASSERT(!items_.empty());
    return items_.front();
  }

  bool isOverloaded() const override { return false; }

  void forEach(const std::function<bool(ItemType&)>& cb) override {
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
