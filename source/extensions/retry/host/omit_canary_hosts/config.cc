#include "extensions/retry/host/omit_canary_hosts/config.h"

#include "envoy/registry/registry.h"
#include "envoy/upstream/retry.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {

REGISTER_FACTORY(OmitCanaryHostsRetryPredicateFactory, Upstream::RetryHostPredicateFactory);

}
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
