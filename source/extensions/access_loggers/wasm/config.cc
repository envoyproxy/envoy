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

AccessLog::InstanceSharedPtr
WasmAccessLogFactory::createAccessLogInstance(const Protobuf::Message& proto_config,
                                              AccessLog::FilterPtr&& filter,
                                              Server::Configuration::FactoryContext& context) {
  const auto& config = MessageUtil::downcastAndValidate<
      const envoy::extensions::access_loggers::wasm::v3::WasmAccessLog&>(
      proto_config, context.messageValidationVisitor());

  auto plugin_config = std::make_unique<Common::Wasm::PluginConfig>(
      config.config(), context.serverFactoryContext(), context.scope(), context.initManager(),
      envoy::config::core::v3::TrafficDirection::UNSPECIFIED, /*metadata=*/nullptr, false);
  auto access_log = std::make_shared<WasmAccessLog>(std::move(plugin_config), std::move(filter));

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
