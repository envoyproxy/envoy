#pragma once

#include "envoy/upstream/retry.h"

#include "source/common/common/logger.h"

#include "library/common/extensions/retry/options/network_configuration/predicate.pb.h"
#include "library/common/extensions/retry/options/network_configuration/predicate.pb.validate.h"
#include "library/common/network/connectivity_manager.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Options {

class NetworkConfigurationRetryOptionsPredicate : public Upstream::RetryOptionsPredicate,
                                                  public Logger::Loggable<Logger::Id::upstream> {
public:
  NetworkConfigurationRetryOptionsPredicate(
      const envoymobile::extensions::retry::options::network_configuration::
          NetworkConfigurationOptionsPredicate&,
      Upstream::RetryExtensionFactoryContext& context);

  Upstream::RetryOptionsPredicate::UpdateOptionsReturn
  updateOptions(const Upstream::RetryOptionsPredicate::UpdateOptionsParameters&) const override;

private:
  Network::ConnectivityManagerSharedPtr connectivity_manager_;
};

} // namespace Options
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
