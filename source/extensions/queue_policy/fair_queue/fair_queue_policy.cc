#include "source/extensions/queue_policy/fair_queue/fair_queue_policy.h"

#include "envoy/registry/registry.h"

#include "source/common/conn_pool/pending_stream.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {
namespace {
using PendingStreamFairQueueFactory = FairQueueFactory<ConnectionPool::PendingStream>;
} // namespace

REGISTER_FACTORY(PendingStreamFairQueueFactory, QueuePolicyFactory<ConnectionPool::PendingStream>);

} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
