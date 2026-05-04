#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/dynamic_module_reporter.h"

#include "envoy/common/exception.h"
#include "envoy/extensions/bootstrap/reverse_tunnel/reporter/dynamic_modules/v3/dynamic_module_reporter.pb.validate.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

DynamicModuleReverseTunnelReporter::DynamicModuleReverseTunnelReporter(
    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    OnReverseTunnelReporterNewType on_reporter_new,
    OnReverseTunnelReporterDestroyType on_reporter_destroy,
    OnReverseTunnelServerInitializedType on_server_initialized,
    OnReverseTunnelConnectedType on_connected, OnReverseTunnelDisconnectedType on_disconnected,
    absl::string_view reporter_config)
    : dynamic_module_(std::move(dynamic_module)), on_reporter_destroy_(on_reporter_destroy),
      on_server_initialized_(on_server_initialized), on_connected_(on_connected),
      on_disconnected_(on_disconnected) {
  envoy_dynamic_module_type_envoy_buffer config_buf = {
      .ptr = const_cast<char*>(reporter_config.data()), .length = reporter_config.size()};
  in_module_reporter_ = on_reporter_new(config_buf);
}

DynamicModuleReverseTunnelReporter::~DynamicModuleReverseTunnelReporter() {
  if (in_module_reporter_ != nullptr) {
    on_reporter_destroy_(in_module_reporter_);
    in_module_reporter_ = nullptr;
  }
}

void DynamicModuleReverseTunnelReporter::onServerInitialized() {
  if (in_module_reporter_ != nullptr && on_server_initialized_ != nullptr) {
    on_server_initialized_(in_module_reporter_);
  }
}

void DynamicModuleReverseTunnelReporter::reportConnectionEvent(absl::string_view node_id,
                                                               absl::string_view cluster_id,
                                                               absl::string_view tenant_id) {
  if (in_module_reporter_ != nullptr && on_connected_ != nullptr) {
    envoy_dynamic_module_type_envoy_buffer node_buf = {.ptr = const_cast<char*>(node_id.data()),
                                                       .length = node_id.size()};
    envoy_dynamic_module_type_envoy_buffer cluster_buf = {
        .ptr = const_cast<char*>(cluster_id.data()), .length = cluster_id.size()};
    envoy_dynamic_module_type_envoy_buffer tenant_buf = {.ptr = const_cast<char*>(tenant_id.data()),
                                                         .length = tenant_id.size()};
    on_connected_(in_module_reporter_, node_buf, cluster_buf, tenant_buf);
  }
}

void DynamicModuleReverseTunnelReporter::reportDisconnectionEvent(absl::string_view node_id,
                                                                  absl::string_view cluster_id) {
  if (in_module_reporter_ != nullptr && on_disconnected_ != nullptr) {
    envoy_dynamic_module_type_envoy_buffer node_buf = {.ptr = const_cast<char*>(node_id.data()),
                                                       .length = node_id.size()};
    envoy_dynamic_module_type_envoy_buffer cluster_buf = {
        .ptr = const_cast<char*>(cluster_id.data()), .length = cluster_id.size()};
    on_disconnected_(in_module_reporter_, node_buf, cluster_buf);
  }
}

ReverseTunnelReporterPtr DynamicModuleReverseTunnelReporterFactory::createReporter(
    Server::Configuration::ServerFactoryContext& context, ProtobufTypes::MessagePtr message) {
  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::bootstrap::reverse_tunnel::reporter::dynamic_modules::v3::
          DynamicModuleReverseTunnelReporter&>(*message, context.messageValidationVisitor());

  const auto& module_config = proto_config.dynamic_module_config();
  auto dynamic_module_or_error = Extensions::DynamicModules::newDynamicModuleByName(
      module_config.name(), module_config.do_not_close(), module_config.load_globally());

  if (!dynamic_module_or_error.ok()) {
    throwEnvoyExceptionOrPanic("Failed to load dynamic module: " +
                               std::string(dynamic_module_or_error.status().message()));
  }

  auto& dynamic_module = dynamic_module_or_error.value();

  auto on_reporter_new = dynamic_module
                             ->getFunctionPointer<OnReverseTunnelReporterNewType>(
                                 "envoy_dynamic_module_on_reverse_tunnel_reporter_new")
                             .value_or(nullptr);
  auto on_reporter_destroy = dynamic_module
                                 ->getFunctionPointer<OnReverseTunnelReporterDestroyType>(
                                     "envoy_dynamic_module_on_reverse_tunnel_reporter_destroy")
                                 .value_or(nullptr);
  auto on_server_initialized = dynamic_module
                                   ->getFunctionPointer<OnReverseTunnelServerInitializedType>(
                                       "envoy_dynamic_module_on_reverse_tunnel_server_initialized")
                                   .value_or(nullptr);
  auto on_connected = dynamic_module
                          ->getFunctionPointer<OnReverseTunnelConnectedType>(
                              "envoy_dynamic_module_on_reverse_tunnel_connected")
                          .value_or(nullptr);
  auto on_disconnected = dynamic_module
                             ->getFunctionPointer<OnReverseTunnelDisconnectedType>(
                                 "envoy_dynamic_module_on_reverse_tunnel_disconnected")
                             .value_or(nullptr);

  if (on_reporter_new == nullptr || on_reporter_destroy == nullptr) {
    throwEnvoyExceptionOrPanic(
        "Dynamic module is missing required reverse tunnel reporter ABI hooks "
        "(envoy_dynamic_module_on_reverse_tunnel_reporter_new / _destroy)");
  }

  std::string reporter_config_str;
  if (proto_config.has_reporter_config()) {
    auto config_or_error = MessageUtil::anyToBytes(proto_config.reporter_config());
    if (!config_or_error.ok()) {
      throwEnvoyExceptionOrPanic("Failed to parse reporter config: " +
                                 std::string(config_or_error.status().message()));
    }
    reporter_config_str = std::move(config_or_error.value());
  }

  return std::make_unique<DynamicModuleReverseTunnelReporter>(
      std::move(dynamic_module), on_reporter_new, on_reporter_destroy, on_server_initialized,
      on_connected, on_disconnected, reporter_config_str);
}

ProtobufTypes::MessagePtr DynamicModuleReverseTunnelReporterFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::bootstrap::reverse_tunnel::reporter::dynamic_modules::
                              v3::DynamicModuleReverseTunnelReporter>();
}

REGISTER_FACTORY(DynamicModuleReverseTunnelReporterFactory, ReverseTunnelReporterFactory);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
