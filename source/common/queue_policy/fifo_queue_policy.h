#pragma once

#include "source/common/queue_policy/list_queue_base.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {

template <class ItemType> class FifoQueue : public ListQueueBase<ItemType> {
public:
  bool isOverloaded() const override { return false; }

protected:
  bool shouldDequeueFromBack() const override { return false; }
};

} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
