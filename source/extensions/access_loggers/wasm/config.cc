#include "source/extensions/access_loggers/wasm/config.h"

#include "envoy/extensions/access_loggers/wasm/v3/wasm.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/access_loggers/wasm/wasm_access_log_impl.h"
#include "source/extensions/common/wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Wasm {

using Common::Wasm::PluginHandleSharedPtrThreadLocal;

AccessLog::InstanceSharedPtr
WasmAccessLogFactory::createAccessLogInstance(const Protobuf::Message& proto_config,
                                              AccessLog::FilterPtr&& filter,
                                              Server::Configuration::FactoryContext& context) {
  const auto& config = MessageUtil::downcastAndValidate<
      const envoy::extensions::access_loggers::wasm::v3::WasmAccessLog&>(
      proto_config, context.messageValidationVisitor());

  auto plugin = std::make_shared<Common::Wasm::Plugin>(
      config.config(), envoy::config::core::v3::TrafficDirection::UNSPECIFIED,
      context.serverFactoryContext().localInfo(), nullptr /* listener_metadata */);

  auto access_log = std::make_shared<WasmAccessLog>(plugin, nullptr, std::move(filter));

  auto callback = [access_log, &context, plugin](Common::Wasm::WasmHandleSharedPtr base_wasm) {
    // NB: the Slot set() call doesn't complete inline, so all arguments must outlive this call.
    auto tls_slot = ThreadLocal::TypedSlot<PluginHandleSharedPtrThreadLocal>::makeUnique(
        context.serverFactoryContext().threadLocal());
    tls_slot->set([base_wasm, plugin](Event::Dispatcher& dispatcher) {
      return std::make_shared<PluginHandleSharedPtrThreadLocal>(
          Common::Wasm::getOrCreateThreadLocalPlugin(base_wasm, plugin, dispatcher));
    });
    access_log->setTlsSlot(std::move(tls_slot));
  };

  if (!Common::Wasm::createWasm(
          plugin, context.scope().createScope(""), context.serverFactoryContext().clusterManager(),
          context.initManager(), context.serverFactoryContext().mainThreadDispatcher(),
          context.serverFactoryContext().api(), context.serverFactoryContext().lifecycleNotifier(),
          remote_data_provider_, std::move(callback))) {
    throw Common::Wasm::WasmException(
        fmt::format("Unable to create Wasm access log {}", plugin->name_));
  }

  context.serverFactoryContext().api().customStatNamespaces().registerStatNamespace(
      Extensions::Common::Wasm::CustomStatNamespace);
  return access_log;
}

ProtobufTypes::MessagePtr WasmAccessLogFactory::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{
      new envoy::extensions::access_loggers::wasm::v3::WasmAccessLog()};
}

std::string WasmAccessLogFactory::name() const { return "envoy.access_loggers.wasm"; }

/**
 * Static registration for the wasm access log. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(WasmAccessLogFactory, Envoy::AccessLog::AccessLogInstanceFactory,
                        "envoy.wasm_access_log");

} // namespace Wasm
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
