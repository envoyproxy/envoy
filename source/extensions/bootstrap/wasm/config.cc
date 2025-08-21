#include "source/extensions/bootstrap/wasm/config.h"

#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/empty_string.h"
#include "source/common/config/datasource.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/common/wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace Wasm {

void WasmServiceExtension::onServerInitialized() { createWasm(context_); }

void WasmServiceExtension::createWasm(Server::Configuration::ServerFactoryContext& context) {
  plugin_config_ = std::make_unique<Common::Wasm::PluginConfig>(
      config_.config(), context, context.scope(), context.initManager(),
      envoy::config::core::v3::TrafficDirection::UNSPECIFIED, /*metadata=*/nullptr,
      config_.singleton());
}

Server::BootstrapExtensionPtr
WasmFactory::createBootstrapExtension(const Protobuf::Message& config,
                                      Server::Configuration::ServerFactoryContext& context) {
  const auto& typed_config =
      MessageUtil::downcastAndValidate<const envoy::extensions::wasm::v3::WasmService&>(
          config, context.messageValidationContext().staticValidationVisitor());
  context.api().customStatNamespaces().registerStatNamespace(
      Extensions::Common::Wasm::CustomStatNamespace);
  return std::make_unique<WasmServiceExtension>(typed_config, context);
}

// /**
//  * Static registration for the wasm factory. @see RegistryFactory.
//  */
REGISTER_FACTORY(WasmFactory, Server::Configuration::BootstrapExtensionFactory);

} // namespace Wasm
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
