#include "source/extensions/queue_policy/codel/codel_queue_policy.h"

#include "envoy/registry/registry.h"

#include "source/common/conn_pool/pending_stream.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {
namespace {
using PendingStreamCodelQueueFactory = CodelQueueFactory<ConnectionPool::PendingStream>;
}

REGISTER_FACTORY(PendingStreamCodelQueueFactory, QueuePolicyFactory<ConnectionPool::PendingStream>);

} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
