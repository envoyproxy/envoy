#pragma once

#include <memory>

#include "envoy/server/factory_context.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {

/**
 * Parent class for bootstrap extensions.
 */
class BootstrapExtension {
public:
  virtual ~BootstrapExtension() = default;

  /**
   * Called when server is done initializing and we have the ServerFactoryContext fully initialized.
   */
  virtual void onServerInitialized() PURE;
};

using BootstrapExtensionPtr = std::unique_ptr<BootstrapExtension>;

namespace Configuration {

/**
 * Implemented for each bootstrap extension and registered via Registry::registerFactory or the
 * convenience class RegisterFactory.
 */
class BootstrapExtensionFactory : public Config::TypedFactory {
public:
  ~BootstrapExtensionFactory() override = default;

  /**
   * Create a particular bootstrap extension implementation from a config proto. If the
   * implementation is unable to produce a factory with the provided parameters, it should throw an
   * EnvoyException. The returned pointer should never be nullptr.
   * @param config the custom configuration for this bootstrap extension type.
   * @param context is the context to use for the extension. Note that the clusterManager is not
   *    yet initialized at this point and **must not** be used.
   */
  virtual BootstrapExtensionPtr createBootstrapExtension(const Protobuf::Message& config,
                                                         ServerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.bootstrap"; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
