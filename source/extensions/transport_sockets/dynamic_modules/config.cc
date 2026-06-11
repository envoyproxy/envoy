#include "source/extensions/transport_sockets/dynamic_modules/config.h"

#include "envoy/extensions/transport_sockets/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/transport_sockets/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/transport_sockets/dynamic_modules/transport_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace DynamicModules {

namespace {

absl::StatusOr<DynamicModuleTransportSocketConfigSharedPtr>
createDynamicModuleConfig(const Protobuf::Message& message,
                          Server::Configuration::TransportSocketFactoryContext& context,
                          bool is_upstream) {
  const auto& proto_config =
      MessageUtil::downcastAndValidate<const envoy::extensions::transport_sockets::dynamic_modules::
                                           v3::DynamicModuleTransportSocket&>(
          message, context.messageValidationVisitor());

  const auto& module_config = proto_config.dynamic_module_config();
  // Transport sockets do not support remote module sources, so no init manager or async callback is
  // passed; only the synchronous local-file and by-name paths can succeed here.
  auto load_result = Envoy::Extensions::DynamicModules::newDynamicModuleByConfig(
      module_config, proto_config.transport_socket_name(), context.serverFactoryContext());
  RETURN_IF_NOT_OK_REF(load_result.status());
  auto dynamic_module = std::move(load_result->loaded);

  std::string socket_config;
  if (proto_config.has_transport_socket_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.transport_socket_config());
    RETURN_IF_NOT_OK_REF(config_or_error.status());
    socket_config = std::move(config_or_error.value());
  }

  return newDynamicModuleTransportSocketConfig(
      proto_config.transport_socket_name(), socket_config, is_upstream,
      proto_config.implements_secure_transport(), std::move(dynamic_module));
}

} // namespace

ProtobufTypes::MessagePtr DynamicModuleTransportSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::transport_sockets::dynamic_modules::v3::DynamicModuleTransportSocket>();
}

absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr>
UpstreamDynamicModuleTransportSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& config,
    Server::Configuration::TransportSocketFactoryContext& context) {
  auto config_or_error = createDynamicModuleConfig(config, context, true);
  RETURN_IF_NOT_OK_REF(config_or_error.status());
  return std::make_unique<DynamicModuleUpstreamTransportSocketFactory>(
      std::move(config_or_error.value()));
}

absl::StatusOr<Network::DownstreamTransportSocketFactoryPtr>
DownstreamDynamicModuleTransportSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& config, Server::Configuration::TransportSocketFactoryContext& context,
    const std::vector<std::string>&) {
  auto config_or_error = createDynamicModuleConfig(config, context, false);
  RETURN_IF_NOT_OK_REF(config_or_error.status());
  return std::make_unique<DynamicModuleDownstreamTransportSocketFactory>(
      std::move(config_or_error.value()));
}

REGISTER_FACTORY(UpstreamDynamicModuleTransportSocketConfigFactory,
                 Server::Configuration::UpstreamTransportSocketConfigFactory);

REGISTER_FACTORY(DownstreamDynamicModuleTransportSocketConfigFactory,
                 Server::Configuration::DownstreamTransportSocketConfigFactory);

} // namespace DynamicModules
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
