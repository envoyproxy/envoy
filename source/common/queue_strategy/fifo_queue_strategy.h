#pragma once

#include <memory>
#include <type_traits>
#include <utility>

#include "source/common/queue_strategy/queue_strategy_base.h"

namespace Envoy {
namespace Extensions {
namespace QueueStrategy {

template <class ItemType> class FifoQueue : public QueueBase<ItemType> {
  using ItemPtrType = std::unique_ptr<ItemType>;
  using Iterator = typename QueueBase<ItemType>::Iterator;

public:
  FifoQueue() = default;
  ~FifoQueue() override = default;

  ConnectionPool::Cancellable* add(ItemPtrType&& item) override {
    static_assert(std::is_base_of<ConnectionPool::Cancellable, ItemType>::value,
                  "Queue item type must inherit from ConnectionPool::Cancellable");
    LinkedList::moveIntoListBack(std::move(item), this->items_);
    return this->items_.back().get();
  }

  ItemPtrType remove(ItemType& item) override {
    static_assert(std::is_base_of<LinkedObject<ItemType>, ItemType>::value,
                  "Queue item type must inherit from LinkedObject");
    return item.removeFromList(this->items_);
  }

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

} // namespace QueueStrategy
} // namespace Extensions
} // namespace Envoy
