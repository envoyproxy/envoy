#pragma once

#include <memory>

#include "envoy/common/exception.h"
#include "envoy/server/factory_context.h"
#include "envoy/server/instance.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/status/statusor.h"

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
  virtual void onServerInitialized(Server::Instance& server) PURE;

  /**
   * Called when the worker thread is initialized.
   */
  virtual void onWorkerThreadInitialized() PURE;
};

using BootstrapExtensionPtr = std::unique_ptr<BootstrapExtension>;

/**
 * Successfully constructed extension, or the error encountered while constructing it.
 */
using BootstrapExtensionCreationResult = absl::StatusOr<BootstrapExtensionPtr>;

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
  [[deprecated("Use createBootstrapExtensionOrError instead")]]
  virtual BootstrapExtensionPtr createBootstrapExtension(const Protobuf::Message& /*config*/,
                                                         ServerFactoryContext& /*context*/) {
    // This is the terminal of the delegation below, so it must not delegate back: an extension
    // that implements neither method lands here.
    throwEnvoyExceptionOrPanic("Bootstrap extension factory implements neither "
                               "createBootstrapExtensionOrError nor the deprecated "
                               "createBootstrapExtension");
    return nullptr;
  }

  /**
   * Create a particular bootstrap extension implementation from a config proto.
   * @param config the custom configuration for this bootstrap extension type.
   * @param context is the context to use for the extension. Note that the clusterManager is not
   *    yet initialized at this point and **must not** be used.
   * @return BootstrapExtensionCreationResult the constructed extension, or an error status.
   */
  virtual BootstrapExtensionCreationResult
  createBootstrapExtensionOrError(const Protobuf::Message& config, ServerFactoryContext& context) {
    // Delegate to the legacy entry point so that extensions which only implement it keep working.
    return createBootstrapExtension(config, context);
  }

  std::string category() const override { return "envoy.bootstrap"; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
