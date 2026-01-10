#include "source/extensions/transport_sockets/dynamic_modules/config.h"

#include "envoy/extensions/transport_sockets/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/transport_sockets/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/extensions/transport_sockets/dynamic_modules/transport_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace DynamicModules {

DynamicModuleTransportSocketFactoryConfig::DynamicModuleTransportSocketFactoryConfig(
    const std::string& socket_name, const std::string& socket_config, bool is_upstream,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module)
    : socket_name_(socket_name), socket_config_(socket_config), is_upstream_(is_upstream),
      dynamic_module_(std::move(dynamic_module)) {}

DynamicModuleTransportSocketFactoryConfig::~DynamicModuleTransportSocketFactoryConfig() {
  if (in_module_config_ != nullptr && on_factory_config_destroy_ != nullptr) {
    on_factory_config_destroy_(in_module_config_);
    in_module_config_ = nullptr;
  }
}

absl::StatusOr<DynamicModuleTransportSocketFactoryConfigSharedPtr>
newDynamicModuleTransportSocketFactoryConfig(
    const std::string& socket_name, const std::string& socket_config, bool is_upstream,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module) {

  auto config = std::make_shared<DynamicModuleTransportSocketFactoryConfig>(
      socket_name, socket_config, is_upstream, std::move(dynamic_module));

// Helper macro to resolve a symbol from the dynamic module.
#define RESOLVE_SYMBOL(field, symbol_name)                                                         \
  do {                                                                                             \
    auto symbol = config->dynamic_module_->getFunctionPointer<decltype(field)>(symbol_name);       \
    if (!symbol.ok()) {                                                                            \
      return absl::InvalidArgumentError(absl::StrCat("Failed to resolve symbol: ", symbol_name,    \
                                                     ": ", symbol.status().message()));            \
    }                                                                                              \
    field = *symbol;                                                                               \
  } while (0)

  // Resolve all required symbols.
  RESOLVE_SYMBOL(config->on_factory_config_destroy_,
                 "envoy_dynamic_module_on_transport_socket_factory_config_destroy");
  RESOLVE_SYMBOL(config->on_transport_socket_new_, "envoy_dynamic_module_on_transport_socket_new");
  RESOLVE_SYMBOL(config->on_transport_socket_destroy_,
                 "envoy_dynamic_module_on_transport_socket_destroy");
  RESOLVE_SYMBOL(config->on_transport_socket_set_callbacks_,
                 "envoy_dynamic_module_on_transport_socket_set_callbacks");
  RESOLVE_SYMBOL(config->on_transport_socket_on_connected_,
                 "envoy_dynamic_module_on_transport_socket_on_connected");
  RESOLVE_SYMBOL(config->on_transport_socket_do_read_,
                 "envoy_dynamic_module_on_transport_socket_do_read");
  RESOLVE_SYMBOL(config->on_transport_socket_do_write_,
                 "envoy_dynamic_module_on_transport_socket_do_write");
  RESOLVE_SYMBOL(config->on_transport_socket_close_,
                 "envoy_dynamic_module_on_transport_socket_close");
  RESOLVE_SYMBOL(config->on_transport_socket_get_protocol_,
                 "envoy_dynamic_module_on_transport_socket_get_protocol");
  RESOLVE_SYMBOL(config->on_transport_socket_get_failure_reason_,
                 "envoy_dynamic_module_on_transport_socket_get_failure_reason");
  RESOLVE_SYMBOL(config->on_transport_socket_can_flush_close_,
                 "envoy_dynamic_module_on_transport_socket_can_flush_close");

  OnTransportSocketFactoryConfigNewType factory_config_new = nullptr;
  RESOLVE_SYMBOL(factory_config_new, "envoy_dynamic_module_on_transport_socket_factory_config_new");

#undef RESOLVE_SYMBOL

  // Create the in-module factory config.
  envoy_dynamic_module_type_envoy_buffer name_buffer = {socket_name.data(), socket_name.size()};
  envoy_dynamic_module_type_envoy_buffer config_buffer = {socket_config.data(),
                                                          socket_config.size()};

  config->in_module_config_ =
      factory_config_new(static_cast<void*>(config.get()), name_buffer, config_buffer, is_upstream);

  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to create in-module transport socket factory config for socket: ", socket_name));
  }

  return config;
}

ProtobufTypes::MessagePtr DynamicModuleTransportSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::transport_sockets::dynamic_modules::v3::
                              DynamicModuleUpstreamTransportSocket>();
}

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

  // Add SNI to hash key if present.
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

absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr>
UpstreamDynamicModuleTransportSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& config,
    Server::Configuration::TransportSocketFactoryContext& context) {
  const auto& proto_config =
      MessageUtil::downcastAndValidate<const envoy::extensions::transport_sockets::dynamic_modules::
                                           v3::DynamicModuleUpstreamTransportSocket&>(
          config, context.messageValidationVisitor());

  // Load the dynamic module.
  auto module_or_error = Envoy::Extensions::DynamicModules::newDynamicModuleByName(
      proto_config.dynamic_module_config().name(),
      proto_config.dynamic_module_config().do_not_close(),
      proto_config.dynamic_module_config().load_globally());
  if (!module_or_error.ok()) {
    return module_or_error.status();
  }

  // Get the socket config as a string.
  std::string socket_config_str;
  if (proto_config.has_socket_config()) {
    socket_config_str = proto_config.socket_config().value();
  }

  // Create the factory config.
  auto factory_config_or_error = newDynamicModuleTransportSocketFactoryConfig(
      proto_config.socket_name(), socket_config_str, true, std::move(module_or_error.value()));
  if (!factory_config_or_error.ok()) {
    return factory_config_or_error.status();
  }

  // Extract ALPN protocols.
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

  // Load the dynamic module.
  auto module_or_error = Envoy::Extensions::DynamicModules::newDynamicModuleByName(
      proto_config.dynamic_module_config().name(),
      proto_config.dynamic_module_config().do_not_close(),
      proto_config.dynamic_module_config().load_globally());
  if (!module_or_error.ok()) {
    return module_or_error.status();
  }

  // Get the socket config as a string.
  std::string socket_config_str;
  if (proto_config.has_socket_config()) {
    socket_config_str = proto_config.socket_config().value();
  }

  // Create the factory config.
  auto factory_config_or_error = newDynamicModuleTransportSocketFactoryConfig(
      proto_config.socket_name(), socket_config_str, false, std::move(module_or_error.value()));
  if (!factory_config_or_error.ok()) {
    return factory_config_or_error.status();
  }

  // Extract ALPN protocols.
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
