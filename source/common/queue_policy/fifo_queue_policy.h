#pragma once

#include <memory>
#include <utility>

#include "source/common/queue_policy/queue_policy_base.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {

template <class ItemType> class FifoQueue : public QueueBase<ItemType> {
  using ItemPtrType = std::unique_ptr<ItemType>;
  using Iterator = typename QueueBase<ItemType>::Iterator;

public:
  FifoQueue() = default;
  ~FifoQueue() override = default;

  ConnectionPool::Cancellable* add(ItemPtrType&& item) override {
    LinkedList::moveIntoListBack(std::move(item), this->items_);
    return this->items_.back().get();
  }

  ItemPtrType remove(ItemType& item) override { return item.removeFromList(this->items_); }

  const ItemPtrType& next() const override { return this->items_.front(); }

  bool isOverloaded() const override { return false; }

  Iterator begin() override {
    auto it = this->items_.begin();
    return Iterator(std::move(it));
  }

  Iterator end() override {
    auto it = this->items_.end();
    return Iterator(std::move(it));
  }
};

} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
