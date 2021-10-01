#include "library/common/extensions/retry/options/network_configuration/predicate.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Options {

NetworkConfigurationRetryOptionsPredicate::NetworkConfigurationRetryOptionsPredicate(
    const envoymobile::extensions::retry::options::network_configuration::
        NetworkConfigurationOptionsPredicate&,
    Upstream::RetryExtensionFactoryContext& context) {
  network_configurator_ = Network::ConfiguratorHandle{context.singletonManager()}.get();
  RELEASE_ASSERT(network_configurator_ != nullptr, "unexpected nullptr network configurator");
}

Upstream::RetryOptionsPredicate::UpdateOptionsReturn
NetworkConfigurationRetryOptionsPredicate::updateOptions(
    const Upstream::RetryOptionsPredicate::UpdateOptionsParameters&) const {
  return {};
}

} // namespace Options
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
