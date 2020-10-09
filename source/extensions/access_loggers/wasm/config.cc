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
  auto access_log =
      std::make_shared<WasmAccessLog>(config.config().root_id(), nullptr, std::move(filter));

  // Create a base WASM to verify that the code loads before setting/cloning the for the
  // individual threads.
  auto plugin = std::make_shared<Common::Wasm::Plugin>(
      config.config().name(), config.config().root_id(), config.config().vm_config().vm_id(),
      config.config().vm_config().runtime(),
      Common::Wasm::anyToBytes(config.config().configuration()), config.config().fail_open(),
      envoy::config::core::v3::TrafficDirection::UNSPECIFIED, context.localInfo(),
      nullptr /* listener_metadata */);

  auto callback = [access_log, &context, plugin](Common::Wasm::WasmHandleSharedPtr base_wasm) {
    auto tls_slot = context.threadLocal().allocateSlot();

    // NB: the Slot set() call doesn't complete inline, so all arguments must outlive this call.
    tls_slot->set(
        [base_wasm,
         plugin](Event::Dispatcher& dispatcher) -> std::shared_ptr<ThreadLocal::ThreadLocalObject> {
          if (!base_wasm) {
            // There is no way to prevent the connection at this point. The user could choose to use
            // an HTTP Wasm plugin and only handle onLog() which would correctly close the
            // connection in onRequestHeaders().
            if (!plugin->fail_open_) {
              ENVOY_LOG(critical, "Plugin configured to fail closed failed to load");
            }
            return nullptr;
          }
          return std::static_pointer_cast<ThreadLocal::ThreadLocalObject>(
              Common::Wasm::getOrCreateThreadLocalWasm(base_wasm, plugin, dispatcher));
        });
    access_log->setTlsSlot(std::move(tls_slot));
  };

  if (!Common::Wasm::createWasm(
          config.config().vm_config(), plugin, context.scope().createScope(""),
          context.clusterManager(), context.initManager(), context.dispatcher(), context.api(),
          context.lifecycleNotifier(), remote_data_provider_, std::move(callback))) {
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
