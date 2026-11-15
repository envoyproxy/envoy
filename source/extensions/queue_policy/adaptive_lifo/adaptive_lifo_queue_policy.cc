#include "source/extensions/queue_policy/adaptive_lifo/adaptive_lifo_queue_policy.h"

#include "envoy/registry/registry.h"

#include "source/common/conn_pool/pending_stream.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {
namespace {
using PendingStreamAdaptiveLifoQueueFactory =
    AdaptiveLifoQueueFactory<ConnectionPool::PendingStream>;
}

REGISTER_FACTORY(PendingStreamAdaptiveLifoQueueFactory,
                 QueuePolicyFactory<ConnectionPool::PendingStream>);

} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
