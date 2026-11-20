#pragma once

#include <iterator>
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
    // Keep any in-progress forEach() traversal pointing at live list nodes.
    for (Traversal* traversal = active_traversals_; traversal != nullptr;
         traversal = traversal->parent_) {
      traversal->onErase(entry->second);
    }
    items_.erase(entry->second);
    item_index_.erase(entry);
  }

  void forEach(absl::FunctionRef<bool(ItemType&)> cb) override {
    if (items_.empty()) {
      return;
    }

    // The items not yet visited always form one contiguous run of items_: a traversal only takes
    // items from either end of the run, and add() appends after it, so items added by the callback
    // are not visited. remove() moves the end of the run past a node that it erases, so the
    // callback may remove any item, including items that have not been visited yet. Nothing here
    // touches an item after its callback returns.
    Traversal traversal(items_, active_traversals_);
    while (!traversal.done_) {
      // Re-evaluated on every step because callback removal can change the dequeue order.
      const bool use_lifo = shouldDequeueFromBack();
      ItemType* item = traversal.take(use_lifo);
      if (!cb(*item)) {
        return;
      }
    }
  }

protected:
  virtual bool shouldDequeueFromBack() const PURE;

private:
  using ItemList = std::list<ItemType*>;

  // The unvisited run [front_, back_] of an in-progress forEach(). Traversals nest if a callback
  // calls forEach() again, so each one links to the traversal that was active before it.
  struct Traversal {
    Traversal(ItemList& items, Traversal*& head)
        : front_(items.begin()), back_(std::prev(items.end())), head_(head), parent_(head) {
      head_ = this;
    }
    ~Traversal() {
      ASSERT(head_ == this);
      head_ = parent_;
    }

    // Takes the next unvisited item from the back of the run if lifo, otherwise from the front.
    ItemType* take(bool lifo) {
      ASSERT(!done_);
      ItemType* item = *(lifo ? back_ : front_);
      if (front_ == back_) {
        done_ = true;
      } else if (lifo) {
        --back_;
      } else {
        ++front_;
      }
      return item;
    }

    // Called before the node at it is erased from the list.
    void onErase(typename ItemList::iterator it) {
      if (done_) {
        return;
      }
      if (it == front_ && it == back_) {
        done_ = true;
      } else if (it == front_) {
        ++front_;
      } else if (it == back_) {
        --back_;
      }
    }

    typename ItemList::iterator front_;
    typename ItemList::iterator back_;
    bool done_{false};
    Traversal*& head_;
    Traversal* const parent_;
  };

  ItemType* nextItem() const {
    ASSERT(!items_.empty());
    return shouldDequeueFromBack() ? items_.back() : items_.front();
  }

  ItemList items_;
  absl::flat_hash_map<ItemType*, typename ItemList::iterator> item_index_;
  Traversal* active_traversals_{nullptr};
};

} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
