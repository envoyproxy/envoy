#pragma once

#include <list>
#include <memory>

#include "source/common/common/assert.h"

namespace Envoy {

/**
 * Helper methods for placing LinkedObject into a list.
 */
namespace LinkedList {

/**
 * Move an item into a linked list at the front.
 * @param item supplies the item to move in.
 * @param list supplies the list to move the item into.
 */
template <typename T, typename U>
void moveIntoList(std::unique_ptr<T>&& item, std::list<std::unique_ptr<U>>& list) {
  ASSERT(!item->inserted_);
  item->inserted_ = true;
  auto position = list.emplace(list.begin(), std::move(item));
  (*position)->entry_ = position;
}

/**
 * Move an item into a linked list at the back.
 * @param item supplies the item to move in.
 * @param list supplies the list to move the item into.
 */
template <typename T, typename U>
void moveIntoListBack(std::unique_ptr<T>&& item, std::list<std::unique_ptr<U>>& list) {
  ASSERT(!item->inserted_);
  item->inserted_ = true;
  auto position = list.emplace(list.end(), std::move(item));
  (*position)->entry_ = position;
}

} // namespace LinkedList

/**
 * Mixin class that allows an object contained in a unique pointer to be easily linked and unlinked
 * from lists.
 */
template <class T> class LinkedObject {
public:
  using ListType = std::list<std::unique_ptr<T>>;

  /**
   * @return the list iterator for the object.
   */
  typename ListType::iterator entry() {
    ASSERT(inserted_);
    return entry_;
  }

  /**
   * @return whether the object is currently inserted into a list.
   */
  bool inserted() { return inserted_; }

  /**
   * Move a linked item from src list to dst list.
   * @param src supplies the list that the item is currently in.
   * @param dst supplies the destination list for the item.
   */
  void moveBetweenLists(ListType& src, ListType& dst) {
    ASSERT(inserted_);
    ASSERT(std::find(src.begin(), src.end(), *entry_) != src.end());

    dst.splice(dst.begin(), src, entry_);
  }

  /**
   * Remove this item from a list.
   * @param list supplies the list to remove from. This item should be in this list.
   */
  std::unique_ptr<T> removeFromList(ListType& list) {
    ASSERT(inserted_);
    ASSERT(std::find(list.begin(), list.end(), *entry_) != list.end());

    std::unique_ptr<T> removed = std::move(*entry_);
    list.erase(entry_);
    inserted_ = false;
    return removed;
  }

protected:
  LinkedObject() = default;

private:
  template <typename U, typename V>
  friend void LinkedList::moveIntoList(std::unique_ptr<U>&&, std::list<std::unique_ptr<V>>&);
  template <typename U, typename V>
  friend void LinkedList::moveIntoListBack(std::unique_ptr<U>&&, std::list<std::unique_ptr<V>>&);

  typename ListType::iterator entry_;
  bool inserted_{false}; // iterators do not have any "invalid" value so we need this boolean for
                         // sanity checking.
};
} // namespace Envoy
