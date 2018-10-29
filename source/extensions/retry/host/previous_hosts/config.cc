#include "extensions/retry/host/previous_hosts/config.h"

#include "envoy/registry/registry.h"
#include "envoy/upstream/retry.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {

static Registry::RegisterFactory<PreviousHostsRetryPredicateFactory,
                                 Upstream::RetryHostPredicateFactory>
    register_;
}
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
