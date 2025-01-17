#pragma once

#include "envoy/common/optref.h"
#include "envoy/common/pure.h"

namespace Envoy {
namespace Config {

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

} // namespace Config
} // namespace Envoy
