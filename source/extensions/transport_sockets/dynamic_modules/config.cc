#include "source/extensions/transport_sockets/dynamic_modules/config.h"

#include "envoy/extensions/transport_sockets/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/transport_sockets/dynamic_modules/stats.h"
#include "source/extensions/transport_sockets/dynamic_modules/transport_socket.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace TransportSockets {

DynamicModuleTransportSocketConfig::DynamicModuleTransportSocketConfig(
    DynamicModulePtr dynamic_module, const std::string& socket_name,
    const std::string& socket_config, bool is_upstream, Stats::Scope& stats_scope)
    : dynamic_module_(std::move(dynamic_module)), socket_name_(socket_name),
      socket_config_(socket_config), is_upstream_(is_upstream),
      stats_(generateDynamicModuleTransportSocketStats(stats_scope)) {}

DynamicModuleTransportSocketConfig::~DynamicModuleTransportSocketConfig() {
  // When the initialization of the dynamic module fails, the in_module_config_ is nullptr,
  // and there's nothing to destroy from the module's point of view.
  if (on_transport_socket_config_destroy_ && in_module_config_) {
    (*on_transport_socket_config_destroy_)(in_module_config_);
  }
}

absl::StatusOr<DynamicModuleTransportSocketConfigSharedPtr>
newDynamicModuleTransportSocketConfig(const absl::string_view socket_name,
                                      const absl::string_view socket_config, bool is_upstream,
                                      DynamicModulePtr dynamic_module, Stats::Scope& stats_scope) {

  // Resolve all required function pointers from the module.
  auto on_config_new = dynamic_module->getFunctionPointer<OnTransportSocketConfigNewType>(
      "envoy_dynamic_module_on_transport_socket_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());

  auto on_config_destroy = dynamic_module->getFunctionPointer<OnTransportSocketConfigDestroyType>(
      "envoy_dynamic_module_on_transport_socket_config_destroy");
  RETURN_IF_NOT_OK_REF(on_config_destroy.status());

  auto on_socket_new = dynamic_module->getFunctionPointer<OnTransportSocketNewType>(
      "envoy_dynamic_module_on_transport_socket_new");
  RETURN_IF_NOT_OK_REF(on_socket_new.status());

  auto on_socket_destroy = dynamic_module->getFunctionPointer<OnTransportSocketDestroyType>(
      "envoy_dynamic_module_on_transport_socket_destroy");
  RETURN_IF_NOT_OK_REF(on_socket_destroy.status());

  auto on_set_callbacks = dynamic_module->getFunctionPointer<OnTransportSocketSetCallbacksType>(
      "envoy_dynamic_module_on_transport_socket_set_callbacks");
  RETURN_IF_NOT_OK_REF(on_set_callbacks.status());

  auto on_protocol = dynamic_module->getFunctionPointer<OnTransportSocketProtocolType>(
      "envoy_dynamic_module_on_transport_socket_protocol");
  RETURN_IF_NOT_OK_REF(on_protocol.status());

  auto on_failure_reason = dynamic_module->getFunctionPointer<OnTransportSocketFailureReasonType>(
      "envoy_dynamic_module_on_transport_socket_failure_reason");
  RETURN_IF_NOT_OK_REF(on_failure_reason.status());

  auto on_can_flush_close = dynamic_module->getFunctionPointer<OnTransportSocketCanFlushCloseType>(
      "envoy_dynamic_module_on_transport_socket_can_flush_close");
  RETURN_IF_NOT_OK_REF(on_can_flush_close.status());

  auto on_close = dynamic_module->getFunctionPointer<OnTransportSocketCloseType>(
      "envoy_dynamic_module_on_transport_socket_close");
  RETURN_IF_NOT_OK_REF(on_close.status());

  auto on_connected = dynamic_module->getFunctionPointer<OnTransportSocketOnConnectedType>(
      "envoy_dynamic_module_on_transport_socket_on_connected");
  RETURN_IF_NOT_OK_REF(on_connected.status());

  auto on_do_read = dynamic_module->getFunctionPointer<OnTransportSocketDoReadType>(
      "envoy_dynamic_module_on_transport_socket_do_read");
  RETURN_IF_NOT_OK_REF(on_do_read.status());

  auto on_do_write = dynamic_module->getFunctionPointer<OnTransportSocketDoWriteType>(
      "envoy_dynamic_module_on_transport_socket_do_write");
  RETURN_IF_NOT_OK_REF(on_do_write.status());

  auto on_get_ssl_info = dynamic_module->getFunctionPointer<OnTransportSocketGetSslInfoType>(
      "envoy_dynamic_module_on_transport_socket_get_ssl_info");
  RETURN_IF_NOT_OK_REF(on_get_ssl_info.status());

  auto on_start_secure_transport =
      dynamic_module->getFunctionPointer<OnTransportSocketStartSecureTransportType>(
          "envoy_dynamic_module_on_transport_socket_start_secure_transport");
  RETURN_IF_NOT_OK_REF(on_start_secure_transport.status());

  auto on_configure_congestion_window =
      dynamic_module->getFunctionPointer<OnTransportSocketConfigureInitialCongestionWindowType>(
          "envoy_dynamic_module_on_transport_socket_configure_initial_congestion_window");
  RETURN_IF_NOT_OK_REF(on_configure_congestion_window.status());

  // Create the config object.
  auto config = std::make_shared<DynamicModuleTransportSocketConfig>(
      std::move(dynamic_module), std::string(socket_name), std::string(socket_config), is_upstream,
      stats_scope);

  // Call the module's config constructor.
  const void* module_config_ptr = (*on_config_new.value())(
      static_cast<void*>(config.get()), socket_name.data(), socket_name.size(),
      socket_config.data(), socket_config.size(), is_upstream);
  if (module_config_ptr == nullptr) {
    return absl::InvalidArgumentError(fmt::format(
        "Failed to create in-module transport socket configuration for socket: {}", socket_name));
  }

  // Store the in-module config and function pointers.
  config->in_module_config_ = module_config_ptr;
  config->on_transport_socket_config_destroy_ = on_config_destroy.value();
  config->on_transport_socket_new_ = on_socket_new.value();
  config->on_transport_socket_destroy_ = on_socket_destroy.value();
  config->on_transport_socket_set_callbacks_ = on_set_callbacks.value();
  config->on_transport_socket_protocol_ = on_protocol.value();
  config->on_transport_socket_failure_reason_ = on_failure_reason.value();
  config->on_transport_socket_can_flush_close_ = on_can_flush_close.value();
  config->on_transport_socket_close_ = on_close.value();
  config->on_transport_socket_on_connected_ = on_connected.value();
  config->on_transport_socket_do_read_ = on_do_read.value();
  config->on_transport_socket_do_write_ = on_do_write.value();
  config->on_transport_socket_get_ssl_info_ = on_get_ssl_info.value();
  config->on_transport_socket_start_secure_transport_ = on_start_secure_transport.value();
  config->on_transport_socket_configure_initial_congestion_window_ =
      on_configure_congestion_window.value();

  ENVOY_LOG_MISC(debug, "Created dynamic module transport socket config: name={}, upstream={}",
                 socket_name, is_upstream);

  return config;
}

absl::StatusOr<std::unique_ptr<UpstreamDynamicModuleTransportSocketFactory>>
UpstreamDynamicModuleTransportSocketFactory::create(
    const envoy::extensions::transport_sockets::dynamic_modules::v3::
        DynamicModuleUpstreamTransportSocket& config,
    Server::Configuration::TransportSocketFactoryContext& context) {

  // Load the dynamic module.
  auto dynamic_module = newDynamicModuleByName(config.dynamic_module_config().name(),
                                               config.dynamic_module_config().do_not_close());
  RETURN_IF_NOT_OK_REF(dynamic_module.status());

  // Serialize the socket configuration.
  std::string socket_config_str;
  if (config.has_socket_config()) {
    socket_config_str = MessageUtil::getJsonStringFromMessageOrError(config.socket_config());
  }

  // Create the socket configuration.
  auto socket_config = newDynamicModuleTransportSocketConfig(
      config.socket_name(), socket_config_str, true, std::move(dynamic_module.value()),
      context.statsScope());
  RETURN_IF_NOT_OK_REF(socket_config.status());

  // Store additional configuration.
  std::string sni = config.sni();
  std::vector<std::string> alpn_protocols(config.alpn_protocols().begin(),
                                          config.alpn_protocols().end());

  ENVOY_LOG_MISC(info,
                 "Created upstream dynamic module transport socket factory: name={}, sni={}, "
                 "alpn_protocols={}",
                 config.socket_name(), sni, absl::StrJoin(alpn_protocols, ","));

  return std::unique_ptr<UpstreamDynamicModuleTransportSocketFactory>(
      new UpstreamDynamicModuleTransportSocketFactory(socket_config.value(), sni, alpn_protocols));
}

UpstreamDynamicModuleTransportSocketFactory::UpstreamDynamicModuleTransportSocketFactory(
    DynamicModuleTransportSocketConfigSharedPtr config, std::string sni,
    std::vector<std::string> alpn_protocols)
    : config_(std::move(config)), sni_(std::move(sni)), alpn_protocols_(std::move(alpn_protocols)) {
}

Network::TransportSocketPtr UpstreamDynamicModuleTransportSocketFactory::createTransportSocket(
    Network::TransportSocketOptionsConstSharedPtr /*options*/,
    Upstream::HostDescriptionConstSharedPtr /*host*/) const {
  return std::make_unique<DynamicModuleTransportSocket>(config_, true);
}

bool UpstreamDynamicModuleTransportSocketFactory::implementsSecureTransport() const {
  // Dynamic module transport sockets are assumed to provide secure transport.
  return true;
}

void UpstreamDynamicModuleTransportSocketFactory::hashKey(
    std::vector<uint8_t>& key, Network::TransportSocketOptionsConstSharedPtr options) const {
  // Add basic factory identity to the hash.
  const std::string factory_id = "dynamic_module_" + config_->getSocketName();
  key.insert(key.end(), factory_id.begin(), factory_id.end());

  // Add options to hash if present.
  if (options && options->serverNameOverride().has_value()) {
    const auto& sni = options->serverNameOverride().value();
    key.insert(key.end(), sni.begin(), sni.end());
  }
}

absl::StatusOr<std::unique_ptr<DownstreamDynamicModuleTransportSocketFactory>>
DownstreamDynamicModuleTransportSocketFactory::create(
    const envoy::extensions::transport_sockets::dynamic_modules::v3::
        DynamicModuleDownstreamTransportSocket& config,
    Server::Configuration::TransportSocketFactoryContext& context) {

  // Load the dynamic module.
  auto dynamic_module = newDynamicModuleByName(config.dynamic_module_config().name(),
                                               config.dynamic_module_config().do_not_close());
  RETURN_IF_NOT_OK_REF(dynamic_module.status());

  // Serialize the socket configuration.
  std::string socket_config_str;
  if (config.has_socket_config()) {
    socket_config_str = MessageUtil::getJsonStringFromMessageOrError(config.socket_config());
  }

  // Create the socket configuration.
  auto socket_config = newDynamicModuleTransportSocketConfig(
      config.socket_name(), socket_config_str, false, std::move(dynamic_module.value()),
      context.statsScope());
  RETURN_IF_NOT_OK_REF(socket_config.status());

  // Store additional configuration.
  bool require_client_certificate =
      config.has_require_client_certificate() && config.require_client_certificate().value();
  std::vector<std::string> alpn_protocols(config.alpn_protocols().begin(),
                                          config.alpn_protocols().end());

  ENVOY_LOG_MISC(info,
                 "Created downstream dynamic module transport socket factory: name={}, "
                 "require_client_cert={}, alpn_protocols={}",
                 config.socket_name(), require_client_certificate,
                 absl::StrJoin(alpn_protocols, ","));

  return std::unique_ptr<DownstreamDynamicModuleTransportSocketFactory>(
      new DownstreamDynamicModuleTransportSocketFactory(
          socket_config.value(), require_client_certificate, alpn_protocols));
}

DownstreamDynamicModuleTransportSocketFactory::DownstreamDynamicModuleTransportSocketFactory(
    DynamicModuleTransportSocketConfigSharedPtr config, bool require_client_certificate,
    std::vector<std::string> alpn_protocols)
    : config_(std::move(config)), require_client_certificate_(require_client_certificate),
      alpn_protocols_(std::move(alpn_protocols)) {}

Network::TransportSocketPtr
DownstreamDynamicModuleTransportSocketFactory::createDownstreamTransportSocket() const {
  return std::make_unique<DynamicModuleTransportSocket>(config_, false);
}

bool DownstreamDynamicModuleTransportSocketFactory::implementsSecureTransport() const {
  // Dynamic module transport sockets are assumed to provide secure transport.
  return true;
}

// Config factory implementations.
ProtobufTypes::MessagePtr
UpstreamDynamicModuleTransportSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::transport_sockets::dynamic_modules::v3::
                              DynamicModuleUpstreamTransportSocket>();
}

absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr>
UpstreamDynamicModuleTransportSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& config,
    Server::Configuration::TransportSocketFactoryContext& context) {

  const auto& upstream_config =
      MessageUtil::downcastAndValidate<const envoy::extensions::transport_sockets::dynamic_modules::
                                           v3::DynamicModuleUpstreamTransportSocket&>(
          config, context.messageValidationVisitor());

  return UpstreamDynamicModuleTransportSocketFactory::create(upstream_config, context);
}

ProtobufTypes::MessagePtr
DownstreamDynamicModuleTransportSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::transport_sockets::dynamic_modules::v3::
                              DynamicModuleDownstreamTransportSocket>();
}

absl::StatusOr<Network::DownstreamTransportSocketFactoryPtr>
DownstreamDynamicModuleTransportSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& config, Server::Configuration::TransportSocketFactoryContext& context,
    const std::vector<std::string>& /* server_names */) {

  const auto& downstream_config =
      MessageUtil::downcastAndValidate<const envoy::extensions::transport_sockets::dynamic_modules::
                                           v3::DynamicModuleDownstreamTransportSocket&>(
          config, context.messageValidationVisitor());

  return DownstreamDynamicModuleTransportSocketFactory::create(downstream_config, context);
}

// Register the factories.
REGISTER_FACTORY(UpstreamDynamicModuleTransportSocketConfigFactory,
                 Server::Configuration::UpstreamTransportSocketConfigFactory);

REGISTER_FACTORY(DownstreamDynamicModuleTransportSocketConfigFactory,
                 Server::Configuration::DownstreamTransportSocketConfigFactory);

} // namespace TransportSockets
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
