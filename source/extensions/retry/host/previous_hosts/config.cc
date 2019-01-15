#include "extensions/retry/host/previous_hosts/config.h"

#include "envoy/registry/registry.h"
#include "envoy/upstream/retry.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {

// NOLINTNEXTLINE(fuchsia-statically-constructed-objects)
static Registry::RegisterFactory<PreviousHostsRetryPredicateFactory,
                                 Upstream::RetryHostPredicateFactory>
    register_;
} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
