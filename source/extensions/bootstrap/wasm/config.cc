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
  auto plugin = std::make_shared<Common::Wasm::Plugin>(
      config_.config(), envoy::config::core::v3::TrafficDirection::UNSPECIFIED, context.localInfo(),
      nullptr);

  auto callback = [this, &context, plugin](Common::Wasm::WasmHandleSharedPtr base_wasm) {
    if (!base_wasm) {
      if (plugin->fail_open_) {
        ENVOY_LOG(error, "Unable to create Wasm service {}", plugin->name_);
      } else {
        ENVOY_LOG(critical, "Unable to create Wasm service {}", plugin->name_);
      }
      return;
    }
    if (config_.singleton()) {
      // Return a Wasm VM which will be stored as a singleton by the Server.
      wasm_service_ = std::make_unique<WasmService>(
          plugin, Common::Wasm::getOrCreateThreadLocalPlugin(base_wasm, plugin,
                                                             context.mainThreadDispatcher()));
      return;
    }
    // Per-thread WASM VM.
    // NB: the Slot set() call doesn't complete inline, so all arguments must outlive this call.
    auto tls_slot =
        ThreadLocal::TypedSlot<Common::Wasm::PluginHandleSharedPtrThreadLocal>::makeUnique(
            context.threadLocal());
    tls_slot->set([base_wasm, plugin](Event::Dispatcher& dispatcher) {
      return std::make_shared<Common::Wasm::PluginHandleSharedPtrThreadLocal>(
          Common::Wasm::getOrCreateThreadLocalPlugin(base_wasm, plugin, dispatcher));
    });
    wasm_service_ = std::make_unique<WasmService>(plugin, std::move(tls_slot));
  };

  if (!Common::Wasm::createWasm(plugin, context.scope().createScope(""), context.clusterManager(),
                                context.initManager(), context.mainThreadDispatcher(),
                                context.api(), context.lifecycleNotifier(), remote_data_provider_,
                                std::move(callback))) {
    // NB: throw if we get a synchronous configuration failures as this is how such failures are
    // reported to xDS.
    throw Common::Wasm::WasmException(
        fmt::format("Unable to create Wasm service {}", plugin->name_));
  }
}

Server::BootstrapExtensionPtr
WasmFactory::createBootstrapExtension(const Protobuf::Message& config,
                                      Server::Configuration::ServerFactoryContext& context) {
  auto typed_config =
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
