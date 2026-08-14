#pragma once

#include <list>

#include "source/common/common/assert.h"
#include "source/common/queue_policy/queue_policy_base.h"

#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {

template <class ItemType> class FifoQueue : public QueueBase<ItemType> {
public:
  FifoQueue() = default;
  ~FifoQueue() override = default;

  size_t size() const override { return items_.size(); }

  bool empty() const override { return items_.empty(); }

  void add(ItemType& item, QueueItemMetadata) override {
    ASSERT(item_index_.find(&item) == item_index_.end());
    item_index_.emplace(&item, items_.insert(items_.end(), &item));
  }

  const ItemType& peek() const override {
    ASSERT(!items_.empty());
    return *items_.front();
  }

  ItemType& peek() override {
    ASSERT(!items_.empty());
    return *items_.front();
  }

  void pop() override {
    ASSERT(!items_.empty());
    remove(*items_.front());
  }

  void remove(ItemType& item) override {
    auto entry = item_index_.find(&item);
    ASSERT(entry != item_index_.end());
    items_.erase(entry->second);
    item_index_.erase(entry);
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
  using ItemList = std::list<ItemType*>;
  ItemList items_;
  absl::flat_hash_map<ItemType*, typename ItemList::iterator> item_index_;
};

} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
