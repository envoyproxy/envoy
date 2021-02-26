#include "extensions/access_loggers/wasm/config.h"

#include "envoy/extensions/access_loggers/wasm/v3/wasm.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/common/logger.h"
#include "common/protobuf/protobuf.h"

#include "extensions/access_loggers/wasm/wasm_access_log_impl.h"
#include "extensions/access_loggers/well_known_names.h"
#include "extensions/common/wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Wasm {

AccessLog::InstanceSharedPtr
WasmAccessLogFactory::createAccessLogInstance(const Protobuf::Message& proto_config,
                                              AccessLog::FilterPtr&& filter,
                                              Server::Configuration::FactoryContext& context) {
  const auto& config = MessageUtil::downcastAndValidate<
      const envoy::extensions::access_loggers::wasm::v3::WasmAccessLog&>(
      proto_config, context.messageValidationVisitor());

  auto plugin = std::make_shared<Common::Wasm::Plugin>(
      config.config(), envoy::config::core::v3::TrafficDirection::UNSPECIFIED, context.localInfo(),
      nullptr /* listener_metadata */);

  auto access_log =
      std::make_shared<WasmAccessLog>(config.config(), plugin, nullptr, std::move(filter));

  auto callback = [access_log, &context, plugin](Common::Wasm::WasmHandleSharedPtr base_wasm) {
    // NB: the Slot set() call doesn't complete inline, so all arguments must outlive this call.
    auto tls_slot =
        ThreadLocal::TypedSlot<Common::Wasm::PluginHandle>::makeUnique(context.threadLocal());
    tls_slot->set([base_wasm, plugin](Event::Dispatcher& dispatcher) {
      return Common::Wasm::getOrCreateThreadLocalPlugin(base_wasm, plugin, dispatcher);
    });
    access_log->setTlsSlot(std::move(tls_slot));
  };

  if (!Common::Wasm::createWasm(access_log->baseConfig(), plugin, context.scope().createScope(""),
                                context.clusterManager(), context.initManager(),
                                context.dispatcher(), context.api(), context.lifecycleNotifier(),
                                remote_data_provider_, std::move(callback))) {
    throw Common::Wasm::WasmException(
        fmt::format("Unable to create Wasm access log {}", plugin->name_));
  }

  return access_log;
}

ProtobufTypes::MessagePtr WasmAccessLogFactory::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{
      new envoy::extensions::access_loggers::wasm::v3::WasmAccessLog()};
}

std::string WasmAccessLogFactory::name() const { return AccessLogNames::get().Wasm; }

/**
 * Static registration for the wasm access log. @see RegisterFactory.
 */
REGISTER_FACTORY(WasmAccessLogFactory,
                 Server::Configuration::AccessLogInstanceFactory){"envoy.wasm_access_log"};

} // namespace Wasm
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
