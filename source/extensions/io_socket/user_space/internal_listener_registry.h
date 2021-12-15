#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/registry/registry.h"
#include "envoy/server/bootstrap_extension_config.h"

#include "source/common/singleton/threadsafe_singleton.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace InternalListener {

class InternalListenerExtension : public Server::Configuration::BootstrapExtension {
public:
  ~InternalListenerExtension() override = default;

  // Server::Configuration::BootstrapExtension
  void onServerInitialized() override;
};

class InternalListenerRegisteryFactory : public Server::Configuration::BootstrapExtensionFactory {
public:
  // Server::Configuration::BootstrapExtensionFactory
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
  }
  std::string name() const override { return "envoy.bootstrap.internal_listener"; };
};

} // namespace InternalListener
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
