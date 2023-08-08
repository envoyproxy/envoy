#pragma once

#include "source/common/queuing/queue_base.h"

#include "fmt/ostream.h"

namespace Envoy {
namespace Queuing {

template <class T> class FifoQueue : public virtual QueueBase<T> {
  using ItemType = T;
  using ItemPtrType = std::unique_ptr<ItemType>;
  using QueueIter = typename QueueBase<T>::iterator;

public:
  FifoQueue() = default;
  virtual ~FifoQueue() = default;

  const ItemPtrType& next() const override { return this->items_.back(); }

  bool isOverloaded() const override { return false; }

  QueueIter begin() override {
    auto it = this->items_.end();
    if (!this->items_.empty()) {
      it--;
    }
    return QueueIter(it, Dir::Backward);
  }

  QueueIter end() override {
    auto it = this->items_.begin();
    if (!this->items_.empty()) {
      it--;
    }

    return QueueIter(it, Dir::Backward);
  }
};

} // namespace Queuing
} // namespace Envoy
