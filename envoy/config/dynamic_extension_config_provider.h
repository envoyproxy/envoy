#pragma once

#include "envoy/common/optref.h"
#include "envoy/common/pure.h"
#include "envoy/config/extension_config_provider.h"
#include "envoy/network/filter.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Config {

using ConfigAppliedCb = std::function<void()>;

class DynamicExtensionConfigProviderBase {
public:
  virtual ~DynamicExtensionConfigProviderBase() = default;

  /**
   * Update the provider with a new configuration. This interface accepts proto rather than a
   * factory callback so that it can be generic over factory types. If instantiating the factory
   * throws, it should only do so on the main thread, before any changes are applied to workers.
   * @param config is the new configuration. It is expected that the configuration has already been
   * validated.
   * @param version_info is the version of the new extension configuration.
   * @param cb the continuation callback for a completed configuration application on all threads.
   * @return an absl status indicating if a non-exception-throwing error was encountered.
   */
  virtual absl::Status onConfigUpdate(const Protobuf::Message& config,
                                      const std::string& version_info,
                                      ConfigAppliedCb applied_on_all_threads) PURE;

  /**
   * Removes the current configuration from the provider.
   * @param cb the continuation callback for a completed configuration application on all threads.
   * @return status indicating if the config was successfully removed.
   */
  virtual absl::Status onConfigRemoved(ConfigAppliedCb applied_on_all_threads) PURE;

  /**
   * Applies the default configuration if one is set, otherwise does nothing.
   */
  virtual absl::Status applyDefaultConfiguration() PURE;
  /**
   * Return Network::ListenerFilterMatcherSharedPtr& the listener filter matcher.
   */
  virtual const Network::ListenerFilterMatcherSharedPtr& getListenerFilterMatcher() PURE;
};

template <class FactoryCallback>
class DynamicExtensionConfigProvider : public DynamicExtensionConfigProviderBase,
                                       public ExtensionConfigProvider<FactoryCallback> {};

} // namespace Config
} // namespace Envoy
