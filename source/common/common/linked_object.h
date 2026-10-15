#pragma once

#include <list>
#include <memory>
#include <type_traits>

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

// Forward declaration needed by IntrusiveListNode.
template <class T> class IntrusiveList;

/**
 * CRTP mixin base class for objects that can be placed in an IntrusiveList<T>.
 *
 * Intrusive linked list design
 * ----------------------------
 * In a traditional std::list<unique_ptr<T>>, the list nodes (with their prev/next
 * pointers) are heap-allocated separately from the objects themselves. An intrusive
 * list instead embeds the prev/next pointers directly inside each object, eliminating
 * the extra allocation and improving cache locality.
 *
 * Ownership model
 * ---------------
 * The IntrusiveList owns every object it contains. Ownership is transferred INTO the
 * list via unique_ptr on insert (the raw pointer is released and the list tracks it
 * through its head/tail pointers and the intrusive next/prev chain). Ownership is
 * transferred back OUT of the list as a unique_ptr on removeFromList().
 *
 * Usage
 * -----
 * Inherit from IntrusiveListNode<T> (CRTP):
 *
 *   class MyObject : public IntrusiveListNode<MyObject> { ... };
 *
 *   IntrusiveList<MyObject> list;
 *   list.pushBack(std::make_unique<MyObject>());
 *   MyObject* obj = list.front();
 *   auto reclaimed = obj->removeFromList(list); // reclaims unique_ptr ownership
 *
 * T must publicly inherit from IntrusiveListNode<T>.
 */
template <class T> class IntrusiveListNode {
public:
  /**
   * @return whether this object is currently inserted into an IntrusiveList.
   */
  bool inserted() const { return inserted_; }

  /**
   * @return pointer to the next element in the list, or nullptr if this is the tail or the node
   *         is not currently inserted.
   */
  T* next() const noexcept { return next_; }

  /**
   * @return pointer to the previous element in the list, or nullptr if this is the head or the
   *         node is not currently inserted.
   */
  T* prev() const noexcept { return prev_; }

  /**
   * Check whether this object is currently in the given list. This is O(n) so should only be used
   * for sanity checking in debug builds.
   * @param list supplies the list to check for membership.
   * @return whether this object is in the list.
   */
  bool insertedIntoList(const IntrusiveList<T>& list) const {
    if (!inserted_) {
      return false;
    }
    // Check if this node is in the list by walking the next chain. This is O(n) but should only
    // be used for sanity checking in debug builds, so it is not a problem.
    const T* current = list.head_;
    while (current != nullptr) {
      if (current == this) {
        return true;
      }
      current = current->next_;
    }
    return false;
  }

  /**
   * Remove this item from the given intrusive list and reclaim ownership.
   *
   * Unlinks this node by stitching its predecessor and successor directly
   * together (or updating the list's head_/tail_ when this is the first or
   * last node). The raw pointer is then wrapped back into a unique_ptr,
   * returning ownership to the caller. After this call, inserted() == false.
   *
   * @param list supplies the intrusive list this item is currently in.
   * @return unique_ptr reclaiming ownership of this item.
   */
  std::unique_ptr<T> removeFromList(IntrusiveList<T>& list) {
    ASSERT(inserted_);
    ASSERT(insertedIntoList(list));
    // Patch up the predecessor's next pointer, or advance the list head.
    if (prev_ != nullptr) {
      prev_->next_ = next_;
    } else {
      // This is the head of the list, so update the head pointer after removal.
      ASSERT(list.head_ == static_cast<T*>(this));
      list.head_ = next_;
    }
    // Patch up the successor's prev pointer, or retreat the list tail.
    if (next_ != nullptr) {
      next_->prev_ = prev_;
    } else {
      // This is the tail of the list, so update the tail pointer after removal.
      ASSERT(list.tail_ == static_cast<T*>(this));
      list.tail_ = prev_;
    }
    next_ = nullptr;
    prev_ = nullptr;
    inserted_ = false;
    list.size_--;
    // Re-wrap the raw pointer: the list no longer owns it.
    return std::unique_ptr<T>(static_cast<T*>(this));
  }

  /**
   * Move this item from src to the front of dst without transferring unique_ptr ownership
   * (the list continues to own the object; only the containing list changes).
   *
   * Implemented by composing removeFromList() and push() so that
   * all pointer-manipulation logic lives in exactly one place.
   *
   * @param src supplies the intrusive list this item is currently in.
   * @param dst supplies the destination intrusive list (must differ from src).
   */
  void moveBetweenLists(IntrusiveList<T>& src, IntrusiveList<T>& dst) {
    ASSERT(&src != &dst);
    dst.push(removeFromList(src));
  }

protected:
  IntrusiveListNode() = default;

private:
  T* next_{nullptr};     // Next node in the list, or nullptr if this is the tail.
  T* prev_{nullptr};     // Previous node in the list, or nullptr if this is the head.
  bool inserted_{false}; // True while this node is owned by an IntrusiveList.

  friend class IntrusiveList<T>;
};

/**
 * Intrusive doubly-linked list that owns all inserted objects.
 *
 * Memory layout
 * -------------
 * The list itself holds only three words: head_, tail_, and size_. There are no
 * separately-allocated list nodes; the link pointers (next_/prev_) live inside
 * each element via the IntrusiveListNode<T> mixin. This makes traversal cache-friendly
 * and avoids the extra allocations that std::list<unique_ptr<T>> requires.
 *
 *   head_ ──► [A] ──► [B] ──► [C] ──► nullptr
 *   tail_ ──────────────────► [C]
 *             [A].prev = nullptr
 *             [B].prev = [A]
 *             [C].prev = [B]
 *
 * Ownership
 * ---------
 * Objects are inserted via unique_ptr (push / pushBack).
 * The list takes ownership by releasing the unique_ptr and tracking the raw pointer
 * through its intrusive chain. Ownership is reclaimed as a unique_ptr via
 * IntrusiveListNode::removeFromList(). The destructor deletes all remaining objects.
 *
 * The list is non-copyable and non-movable to keep ownership semantics simple.
 *
 * T must publicly inherit from IntrusiveListNode<T>.
 */
template <class T> class IntrusiveList {
private:
  // Helper for internal traversal; uses friend access to IntrusiveListNode<T>.
  static T* nextNode(T* node) noexcept { return node->next_; }

public:
  IntrusiveList() = default;

  // Deletes all remaining owned objects by walking the intrusive next chain.
  ~IntrusiveList() {
    T* current = head_;
    while (current != nullptr) {
      T* next = nextNode(current);
      delete current;
      current = next;
    }
  }

  // Not copyable or movable; ownership of raw pointers must not be aliased.
  IntrusiveList(const IntrusiveList&) = delete;
  IntrusiveList& operator=(const IntrusiveList&) = delete;
  IntrusiveList(IntrusiveList&&) = delete;
  IntrusiveList& operator=(IntrusiveList&&) = delete;

  /**
   * @return pointer to the first element, or nullptr if the list is empty.
   */
  T* front() noexcept { return head_; }
  /**
   * @return pointer to the first element, or nullptr if the list is empty.
   */
  const T* front() const noexcept { return head_; }
  /**
   * @return pointer to the last element, or nullptr if the list is empty.
   */
  T* back() noexcept { return tail_; }
  /**
   * @return pointer to the last element, or nullptr if the list is empty.
   */
  const T* back() const noexcept { return tail_; }
  /**
   * @return the number of elements currently in the list.
   */
  size_t size() const noexcept { return size_; }
  /**
   * @return true if the list contains no elements.
   */
  bool empty() const noexcept { return size_ == 0; }

  /**
   * Transfer ownership of item into the front of the list.
   *
   * Releases the unique_ptr, marks the node as inserted, and prepends it
   * by making it the new head_ (updating the old head_'s prev_ pointer, or
   * setting tail_ if the list was empty).
   *
   * @param item the object to insert; must not already be in a list.
   */
  template <typename U> void push(std::unique_ptr<U>&& item) {
    // Compile-time checks to ensure that the item can be safely owned by this list.
    static_assert(std::is_base_of<T, U>::value, "push requires U to be T or derived from T.");
    if constexpr (!std::is_same<T, U>::value) {
      static_assert(std::has_virtual_destructor<T>::value,
                    "Deleting derived U objects through base T* is undefined unless T has a "
                    "virtual destructor.");
    }

    ASSERT(!item->inserted_);
    T* raw = item.release();
    raw->inserted_ = true;
    raw->prev_ = nullptr;
    raw->next_ = head_;
    if (head_ != nullptr) {
      head_->prev_ = raw;
    } else {
      // List was empty; the new node is also the tail.
      tail_ = raw;
    }
    head_ = raw;
    size_++;
  }

  /**
   * Transfer ownership of item onto the back of the list.
   *
   * Releases the unique_ptr, marks the node as inserted, and appends it
   * by making it the new tail_ (updating the old tail_'s next_ pointer, or
   * setting head_ if the list was empty).
   *
   * @param item the object to insert; must not already be in a list.
   */
  template <typename U> void pushBack(std::unique_ptr<U>&& item) {
    // Compile-time checks to ensure that the item can be safely owned by this list.
    static_assert(std::is_base_of<T, U>::value, "pushBack requires U to be T or derived from T.");
    if constexpr (!std::is_same<T, U>::value) {
      static_assert(std::has_virtual_destructor<T>::value,
                    "Deleting derived U objects through base T* is undefined unless T has a "
                    "virtual destructor.");
    }

    ASSERT(!item->inserted_);
    T* raw = item.release();
    raw->inserted_ = true;
    raw->next_ = nullptr;
    raw->prev_ = tail_;
    if (tail_ != nullptr) {
      tail_->next_ = raw;
    } else {
      // List was empty; the new node is also the head.
      head_ = raw;
    }
    tail_ = raw;
    size_++;
  }

private:
  T* head_{nullptr}; // First element, or nullptr when the list is empty.
  T* tail_{nullptr}; // Last element, or nullptr when the list is empty.
  size_t size_{0};   // Number of elements currently owned by this list.

  friend class IntrusiveListNode<T>;
};

} // namespace Envoy
