#include "extensions/bootstrap/wasm/config.h"

#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"

#include "common/common/empty_string.h"
#include "common/config/datasource.h"
#include "common/protobuf/utility.h"

#include "extensions/common/wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace Wasm {

static const std::string INLINE_STRING = "<inline>";

void WasmFactory::createWasm(const envoy::extensions::wasm::v3::WasmService& config,
                             Server::Configuration::ServerFactoryContext& context,
                             CreateWasmServiceCallback&& cb) {
  auto plugin = std::make_shared<Common::Wasm::Plugin>(
      config.config().name(), config.config().root_id(), config.config().vm_config().vm_id(),
      config.config().vm_config().runtime(),
      Common::Wasm::anyToBytes(config.config().configuration()), config.config().fail_open(),
      envoy::config::core::v3::TrafficDirection::UNSPECIFIED, context.localInfo(), nullptr);

  bool singleton = config.singleton();
  auto callback = [&context, singleton, plugin, cb](Common::Wasm::WasmHandleSharedPtr base_wasm) {
    if (!base_wasm) {
      if (plugin->fail_open_) {
        ENVOY_LOG(error, "Unable to create Wasm service {}", plugin->name_);
      } else {
        ENVOY_LOG(critical, "Unable to create Wasm service {}", plugin->name_);
      }
      return;
    }
    if (singleton) {
      // Return a Wasm VM which will be stored as a singleton by the Server.
      cb(std::make_unique<WasmService>(
          Common::Wasm::getOrCreateThreadLocalWasm(base_wasm, plugin, context.dispatcher())));
      return;
    }
    // Per-thread WASM VM.
    // NB: the Slot set() call doesn't complete inline, so all arguments must outlive this call.
    auto tls_slot = context.threadLocal().allocateSlot();
    tls_slot->set([base_wasm, plugin](Event::Dispatcher& dispatcher) {
      return std::static_pointer_cast<ThreadLocal::ThreadLocalObject>(
          Common::Wasm::getOrCreateThreadLocalWasm(base_wasm, plugin, dispatcher));
    });
    cb(std::make_unique<WasmService>(std::move(tls_slot)));
  };

  if (!Common::Wasm::createWasm(
          config.config().vm_config(), plugin, context.scope().createScope(""),
          context.clusterManager(), context.initManager(), context.dispatcher(), context.api(),
          context.lifecycleNotifier(), remote_data_provider_, std::move(callback))) {
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

  auto wasm_service_extension = std::make_unique<WasmServiceExtension>();
  createWasm(typed_config, context,
             [extension = wasm_service_extension.get()](WasmServicePtr wasm) {
               extension->wasm_service_ = std::move(wasm);
             });
  return wasm_service_extension;
}

// /**
//  * Static registration for the wasm factory. @see RegistryFactory.
//  */
REGISTER_FACTORY(WasmFactory, Server::Configuration::BootstrapExtensionFactory);

} // namespace Wasm
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
