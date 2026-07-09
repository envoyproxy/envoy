#include "source/extensions/queue_policy/fifo/fifo_queue_policy.h"

#include "envoy/registry/registry.h"

#include "source/common/conn_pool/pending_stream.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {
namespace {
using PendingStreamFifoQueueFactory = FifoQueueFactory<ConnectionPool::PendingStream>;
}

REGISTER_FACTORY(PendingStreamFifoQueueFactory, QueuePolicyFactory<ConnectionPool::PendingStream>);

} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
