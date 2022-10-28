#pragma once

#include "envoy/common/optref.h"
#include "envoy/common/pure.h"
#include "envoy/network/filter.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Config {

using ConfigAppliedCb = std::function<void()>;

/**
 * A provider for extension configurations obtained either statically or via
 * the extension configuration discovery service. Dynamically updated extension
 * configurations may share subscriptions across extension config providers.
 */
template <class FactoryCallback> class ExtensionConfigProvider {
public:
  virtual ~ExtensionConfigProvider() = default;

  /**
   * Get the extension configuration resource name.
   **/
  virtual const std::string& name() PURE;

  /**
   * @return FactoryCallback an extension factory callback. Note that if the
   * provider has not yet performed an initial configuration load and no
   * default is provided, an empty optional will be returned. The factory
   * callback is the latest version of the extension configuration, and should
   * generally apply only to new requests and connections.
   */
  virtual OptRef<FactoryCallback> config() PURE;
};

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
   */
  virtual void onConfigUpdate(const Protobuf::Message& config, const std::string& version_info,
                              ConfigAppliedCb applied_on_all_threads) PURE;

  /**
   * Removes the current configuration from the provider.
   * @param cb the continuation callback for a completed configuration application on all threads.
   */
  virtual void onConfigRemoved(ConfigAppliedCb applied_on_all_threads) PURE;

  /**
   * Applies the default configuration if one is set, otherwise does nothing.
   */
  virtual void applyDefaultConfiguration() PURE;
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
