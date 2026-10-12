#pragma once

#include "source/common/queue_policy/adaptive_lifo_queue.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {

template <class ItemType> class FifoQueue : public AdaptiveLifoQueue<ItemType> {};

} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
