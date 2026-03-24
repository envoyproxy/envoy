#include "source/extensions/queue_strategy/fifo/fifo_queue_strategy.h"

namespace Envoy {
namespace Extensions {
namespace QueueStrategy {

/**
 * Static registration for the FifoQueueFactory.
 */
template <class ItemType>
static auto REGISTER_FACTORY =
    new Envoy::Registry::RegisterFactory<FifoQueueFactory<ItemType>,
                                         QueueStrategy::QueueStrategyFactory<ItemType>>();

} // namespace QueueStrategy
} // namespace Extensions
} // namespace Envoy
