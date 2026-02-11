#include "source/extensions/transport_sockets/dynamic_modules/factory.h"

#include "envoy/extensions/transport_sockets/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/transport_sockets/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/transport_sockets/dynamic_modules/transport_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace DynamicModules {

// UpstreamDynamicModuleTransportSocketFactory implementation.

UpstreamDynamicModuleTransportSocketFactory::UpstreamDynamicModuleTransportSocketFactory(
    DynamicModuleTransportSocketFactoryConfigSharedPtr config, const std::string& sni,
    const std::vector<std::string>& alpn_protocols)
    : factory_config_(std::move(config)), sni_(sni), alpn_protocols_(alpn_protocols) {}

void UpstreamDynamicModuleTransportSocketFactory::hashKey(
    std::vector<uint8_t>& key, Network::TransportSocketOptionsConstSharedPtr options) const {
  // Add socket name to hash key.
  const auto& name = factory_config_->socket_name();
  key.insert(key.end(), name.begin(), name.end());

  // Add SNI to hash key, using override if present.
  std::string sni = sni_;
  if (options && options->serverNameOverride().has_value()) {
    sni = options->serverNameOverride().value();
  }
  if (!sni.empty()) {
    key.insert(key.end(), sni.begin(), sni.end());
  }

  // Add ALPN protocols to hash key.
  for (const auto& alpn : alpn_protocols_) {
    key.insert(key.end(), alpn.begin(), alpn.end());
  }
}

Network::TransportSocketPtr UpstreamDynamicModuleTransportSocketFactory::createTransportSocket(
    Network::TransportSocketOptionsConstSharedPtr, Upstream::HostDescriptionConstSharedPtr) const {
  return std::make_unique<DynamicModuleTransportSocket>(factory_config_);
}

// DownstreamDynamicModuleTransportSocketFactory implementation.

DownstreamDynamicModuleTransportSocketFactory::DownstreamDynamicModuleTransportSocketFactory(
    DynamicModuleTransportSocketFactoryConfigSharedPtr config, bool require_client_certificate,
    const std::vector<std::string>& alpn_protocols)
    : factory_config_(std::move(config)), require_client_certificate_(require_client_certificate),
      alpn_protocols_(alpn_protocols) {}

Network::TransportSocketPtr
DownstreamDynamicModuleTransportSocketFactory::createDownstreamTransportSocket() const {
  return std::make_unique<DynamicModuleTransportSocket>(factory_config_);
}

// Config factory implementations.

ProtobufTypes::MessagePtr
UpstreamDynamicModuleTransportSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::transport_sockets::dynamic_modules::v3::
                              DynamicModuleUpstreamTransportSocket>();
}

ProtobufTypes::MessagePtr
DownstreamDynamicModuleTransportSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::transport_sockets::dynamic_modules::v3::
                              DynamicModuleDownstreamTransportSocket>();
}

absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr>
UpstreamDynamicModuleTransportSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& config,
    Server::Configuration::TransportSocketFactoryContext& context) {
  const auto& proto_config =
      MessageUtil::downcastAndValidate<const envoy::extensions::transport_sockets::dynamic_modules::
                                           v3::DynamicModuleUpstreamTransportSocket&>(
          config, context.messageValidationVisitor());

  const auto& module_config = proto_config.dynamic_module_config();
  auto dynamic_module = Envoy::Extensions::DynamicModules::newDynamicModuleByName(
      module_config.name(), module_config.do_not_close(), module_config.load_globally());
  if (!dynamic_module.ok()) {
    return dynamic_module.status();
  }

  std::string socket_config_str;
  if (proto_config.has_socket_config()) {
    auto config_or_error = MessageUtil::anyToBytes(proto_config.socket_config());
    RETURN_IF_NOT_OK_REF(config_or_error.status());
    socket_config_str = std::move(config_or_error.value());
  }

  auto factory_config_or_error = newDynamicModuleTransportSocketFactoryConfig(
      proto_config.socket_name(), socket_config_str, true, std::move(dynamic_module.value()));
  if (!factory_config_or_error.ok()) {
    return factory_config_or_error.status();
  }

  std::vector<std::string> alpn_protocols(proto_config.alpn_protocols().begin(),
                                          proto_config.alpn_protocols().end());

  return std::make_unique<UpstreamDynamicModuleTransportSocketFactory>(
      std::move(factory_config_or_error.value()), proto_config.sni(), alpn_protocols);
}

absl::StatusOr<Network::DownstreamTransportSocketFactoryPtr>
DownstreamDynamicModuleTransportSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& config, Server::Configuration::TransportSocketFactoryContext& context,
    const std::vector<std::string>&) {
  const auto& proto_config =
      MessageUtil::downcastAndValidate<const envoy::extensions::transport_sockets::dynamic_modules::
                                           v3::DynamicModuleDownstreamTransportSocket&>(
          config, context.messageValidationVisitor());

  const auto& module_config = proto_config.dynamic_module_config();
  auto dynamic_module = Envoy::Extensions::DynamicModules::newDynamicModuleByName(
      module_config.name(), module_config.do_not_close(), module_config.load_globally());
  if (!dynamic_module.ok()) {
    return dynamic_module.status();
  }

  std::string socket_config_str;
  if (proto_config.has_socket_config()) {
    auto config_or_error = MessageUtil::anyToBytes(proto_config.socket_config());
    RETURN_IF_NOT_OK_REF(config_or_error.status());
    socket_config_str = std::move(config_or_error.value());
  }

  auto factory_config_or_error = newDynamicModuleTransportSocketFactoryConfig(
      proto_config.socket_name(), socket_config_str, false, std::move(dynamic_module.value()));
  if (!factory_config_or_error.ok()) {
    return factory_config_or_error.status();
  }

  std::vector<std::string> alpn_protocols(proto_config.alpn_protocols().begin(),
                                          proto_config.alpn_protocols().end());

  bool require_client_certificate = proto_config.has_require_client_certificate() &&
                                    proto_config.require_client_certificate().value();

  return std::make_unique<DownstreamDynamicModuleTransportSocketFactory>(
      std::move(factory_config_or_error.value()), require_client_certificate, alpn_protocols);
}

LEGACY_REGISTER_FACTORY(UpstreamDynamicModuleTransportSocketConfigFactory,
                        Server::Configuration::UpstreamTransportSocketConfigFactory,
                        "dynamic_modules");

LEGACY_REGISTER_FACTORY(DownstreamDynamicModuleTransportSocketConfigFactory,
                        Server::Configuration::DownstreamTransportSocketConfigFactory,
                        "dynamic_modules");

} // namespace DynamicModules
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
