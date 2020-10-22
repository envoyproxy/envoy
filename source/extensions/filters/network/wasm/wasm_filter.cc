#include "extensions/filters/network/wasm/wasm_filter.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/enum_to_int.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Wasm {

FilterConfig::FilterConfig(const envoy::extensions::filters::network::wasm::v3::Wasm& config,
                           Server::Configuration::FactoryContext& context)
    : tls_slot_(context.threadLocal().allocateSlot()) {
  plugin_ = std::make_shared<Common::Wasm::Plugin>(
      config.config().name(), config.config().root_id(), config.config().vm_config().vm_id(),
      config.config().vm_config().runtime(),
      Common::Wasm::anyToBytes(config.config().configuration()), config.config().fail_open(),
      context.direction(), context.localInfo(), &context.listenerMetadata());

  auto plugin = plugin_;
  auto callback = [plugin, this](Common::Wasm::WasmHandleSharedPtr base_wasm) {
    // NB: the Slot set() call doesn't complete inline, so all arguments must outlive this call.
    tls_slot_->set(
        [base_wasm,
         plugin](Event::Dispatcher& dispatcher) -> std::shared_ptr<ThreadLocal::ThreadLocalObject> {
          if (!base_wasm) {
            return nullptr;
          }
          return std::static_pointer_cast<ThreadLocal::ThreadLocalObject>(
              Common::Wasm::getOrCreateThreadLocalWasm(base_wasm, plugin, dispatcher));
        });
  };

  if (!Common::Wasm::createWasm(
          config.config().vm_config(), plugin_, context.scope().createScope(""),
          context.clusterManager(), context.initManager(), context.dispatcher(), context.api(),
          context.lifecycleNotifier(), remote_data_provider_, std::move(callback))) {
    throw Common::Wasm::WasmException(
        fmt::format("Unable to create Wasm network filter {}", plugin->name_));
  }
}

} // namespace Wasm
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
