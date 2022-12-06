#include "library/common/extensions/retry/options/network_configuration/config.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Options {

REGISTER_FACTORY(NetworkConfigurationRetryOptionsPredicateFactory,
                 Upstream::RetryOptionsPredicateFactory);

}
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
