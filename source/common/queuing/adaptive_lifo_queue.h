#pragma once

#include "source/common/queuing/queue_base.h"

namespace Envoy {
namespace Queuing {

// Base class that handles queuing for pending streams.
template <class T> class AdaptiveLifoQueue : public virtual QueueBase<T> {
  using ItemType = T;
  using ItemPtrType = std::unique_ptr<ItemType>;
  using QueueIter = typename QueueBase<T>::iterator;

public:
  AdaptiveLifoQueue(uint32_t threshold) : threshold_(threshold) {}
  virtual ~AdaptiveLifoQueue() = default;

  const ItemPtrType& next() const override {
    if (isOverloaded()) { /* LIFO */
      return this->items_.front();
    }
    return this->items_.back();
  };

  bool isOverloaded() const override { return this->items_.size() > threshold_; }

  QueueIter begin() override {
    if (isOverloaded()) { /* LIFO */
      return QueueIter(this->items_.begin(), Dir::Forward);
    }
    auto it = this->items_.end();
    if (!this->items_.empty()) {
      it--;
    }
    return QueueIter(it, Dir::Backward);
  }

  QueueIter end() override {
    if (isOverloaded()) { /* LIFO */
      return QueueIter(this->items_.end(), Dir::Forward);
    }

    auto it = this->items_.begin();
    if (!this->items_.empty()) {
      it--;
    }

    return QueueIter(it, Dir::Backward);
  }

private:
  uint32_t threshold_;
};

} // namespace Queuing
} // namespace Envoy
