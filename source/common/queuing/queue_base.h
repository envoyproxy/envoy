#pragma once

#include <list>

#include "envoy/common/conn_pool.h"

namespace Envoy {
namespace Queuing {

enum class Dir {
  Forward,
  Backward,
};

// Base class that handles queuing for pending streams.
template <class T> class QueueBase {
  using ItemType = T;
  using ItemPtrType = std::unique_ptr<ItemType>;
  using ListType = std::list<ItemPtrType>;

public:
  QueueBase() = default;
  virtual ~QueueBase() = default;

  // Move constructor.
  QueueBase(QueueBase&& other) noexcept { *this = std::move(other); }
  operator std::list<ItemPtrType> &&() { return std::move(items_); }

  QueueBase& operator=(QueueBase&& other) {
    if (this != &other) {
      other.items_ = std::move(items_);
    }

    return *this;
  }

  virtual uint32_t size() const { return items_.size(); }

  virtual bool empty() const { return items_.empty(); }

  virtual ConnectionPool::Cancellable* add(ItemPtrType&& item) {
    LinkedList::moveIntoList(std::move(item), items_);
    return items_.front().get();
  }

  virtual const ItemPtrType& next() const PURE;
  virtual bool isOverloaded() const PURE;

  ItemPtrType remove(ItemType& item) { return item.removeFromList(items_); }

  class Iterator : public std::iterator<std::forward_iterator_tag, // iterator_category
                                        ItemPtrType> {
  public:
    explicit Iterator(typename QueueBase::ListType::iterator it, Dir dir) : it_(it), dir_(dir) {}
    Iterator& operator++() {
      switch (dir_) {
      case Dir::Forward:
        it_++;
        break;
      case Dir::Backward:
        it_--;
        break;
      }
      return *this;
    }
    bool operator==(const Iterator& other) const { return other.it_ == it_; }
    bool operator!=(const Iterator& other) const { return other.it_ != it_; }
    ItemPtrType& operator*() const { return *it_; }

  private:
    typename QueueBase::ListType::iterator it_;
    Dir dir_;
  };
  virtual Iterator begin() PURE;
  virtual Iterator end() PURE;

protected:
  ListType items_;
};

} // namespace Queuing
} // namespace Envoy
