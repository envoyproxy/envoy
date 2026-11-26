#pragma once

#include <vector>

#include "envoy/common/callback.h"
#include "envoy/config/extension_config_provider.h"
#include "envoy/network/filter.h"
#include "envoy/stats/stats.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Filter {

/**
 * Owns the worker-local read filters for one ECDS resource. The listener's factory callback
 * owns the provider and outlives the worker filter.
 */
class UdpListenerFilter : public Network::UdpListenerReadFilter,
                          private Network::UdpListenerFilterManager,
                          private Logger::Loggable<Logger::Id::filter> {
public:
  UdpListenerFilter(Config::ExtensionConfigProvider<Network::UdpListenerFilterFactoryCb>& provider,
                    Network::UdpReadFilterCallbacks& callbacks, Stats::Counter& update_failures);

  ~UdpListenerFilter() override;
  Network::FilterStatus onData(Network::UdpRecvData& data) override;
  Network::FilterStatus onReceiveError(Api::IoError::IoErrorCode error_code) override;

private:
  void update();
  void addReadFilter(Network::UdpListenerReadFilterPtr&& filter) override;

  Config::ExtensionConfigProvider<Network::UdpListenerFilterFactoryCb>& provider_;
  Stats::Counter& update_failures_;
  std::vector<Network::UdpListenerReadFilterPtr> filters_;
  bool ready_{true};
  // Remove the worker callback before destroying filters, which may use the listener on teardown.
  Common::CallbackHandlePtr update_handle_;
};

} // namespace Filter
} // namespace Envoy
