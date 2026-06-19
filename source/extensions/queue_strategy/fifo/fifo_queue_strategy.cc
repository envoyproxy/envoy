#include "source/extensions/queue_strategy/fifo/fifo_queue_strategy.h"

#include "envoy/registry/registry.h"

#include "source/common/conn_pool/pending_stream.h"

namespace Envoy {
namespace Extensions {
namespace QueueStrategy {
namespace {
using PendingStreamFifoQueueFactory = FifoQueueFactory<ConnectionPool::PendingStream>;
}

REGISTER_FACTORY(PendingStreamFifoQueueFactory,
                 QueueStrategyFactory<ConnectionPool::PendingStream>);

} // namespace QueueStrategy
} // namespace Extensions
} // namespace Envoy
