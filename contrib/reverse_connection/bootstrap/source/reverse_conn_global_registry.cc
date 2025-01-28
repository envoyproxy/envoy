#include "contrib/reverse_connection/bootstrap/source/reverse_conn_global_registry.h"

#include "envoy/event/dispatcher.h"
#include "envoy/singleton/manager.h"

#include "source/common/singleton/threadsafe_singleton.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

SINGLETON_MANAGER_REGISTRATION(reverse_conn_registry);

Bootstrap::ReverseConnection::RCThreadLocalRegistry* ReverseConnRegistry::getLocalRegistry() {
  // The tls slot may not initialized yet. This can happen in production when Envoy is starting.
  if (!tls_slot_) {
    return nullptr;
  }
  if (auto opt = tls_slot_->get(); opt.has_value()) {
    return &opt.value().get();
  }
  return nullptr;
}

absl::StatusOr<Network::ReverseConnectionListenerConfigPtr> ReverseConnRegistry::fromAnyConfig(
    const google::protobuf::Any& config) {
  envoy::extensions::reverse_connection::reverse_connection_listener_config::v3alpha::ReverseConnectionListenerConfig reverse_conn_config;
  if (!config.UnpackTo(&reverse_conn_config)) {
    return absl::InvalidArgumentError("Failed to unpack reverse connection listener config");
  }
  if (reverse_conn_config.src_node_id().empty()) {
    return absl::InvalidArgumentError("Source node ID is missing in reverse connection listener config");
  } else if (reverse_conn_config.remote_cluster_to_conn_count().empty()) {
    return absl::InvalidArgumentError(
        "Remote cluster to connection count map is missing in reverse connection listener config");
  }

  Network::ReverseConnParamsPtr rc_params =
      std::make_unique<Network::ReverseConnectionListenerConfig::ReverseConnParams>(
          Network::ReverseConnectionListenerConfig::ReverseConnParams{
              reverse_conn_config.src_node_id(),              // src_node_id_
              reverse_conn_config.src_cluster_id(),           // src_cluster_id_
              reverse_conn_config.src_tenant_id(),            // src_tenant_id_
              absl::flat_hash_map<std::string, uint32_t>()}); // remote_cluster_to_conn_count_map_
  ENVOY_LOG(debug, "src_node_id_: {} src_cluster_id_: {} src_tenant_id_: {}",
            rc_params->src_node_id_, rc_params->src_cluster_id_, rc_params->src_tenant_id_);
  for (const auto& remote_cluster_conn_pair : reverse_conn_config.remote_cluster_to_conn_count()) {
    ENVOY_LOG(debug, "Remote cluster: {}, conn count: {}", remote_cluster_conn_pair.cluster_name(),
              remote_cluster_conn_pair.reverse_connection_count().value());
    rc_params->remote_cluster_to_conn_count_map_.emplace(
        remote_cluster_conn_pair.cluster_name(),
        remote_cluster_conn_pair.reverse_connection_count().value());
  }
  return std::make_unique<Envoy::Extensions::ReverseConnection::ReverseConnectionListenerConfigImpl>(std::move(rc_params), *this);
}


ReverseConnExtension::ReverseConnExtension(
    Server::Configuration::ServerFactoryContext& server_context,
    const envoy::extensions::bootstrap::reverse_connection::v3alpha::ReverseConnection& config)
    : server_context_(server_context),
      global_tls_registry_(std::make_shared<ReverseConnRegistry>()) {
  stat_prefix_ = PROTOBUF_GET_STRING_OR_DEFAULT(config, stat_prefix, "reverse_connection");
  ENVOY_LOG_MISC(debug,
                 "ReverseConnExtension: creating reverse connection registry with stat_prefix: {}",
                 stat_prefix_);
  server_context_.singletonManager().getTyped<ReverseConnRegistry>(
      SINGLETON_MANAGER_REGISTERED_NAME(reverse_conn_registry),
      [registry = global_tls_registry_]() { return registry; });
}

void ReverseConnExtension::onServerInitialized() {
  std::shared_ptr<ReverseConnRegistry> reverse_conn_registry =
      server_context_.singletonManager().getTyped<ReverseConnRegistry>(
          SINGLETON_MANAGER_REGISTERED_NAME(reverse_conn_registry));
  ASSERT(reverse_conn_registry == global_tls_registry_);
  ASSERT(reverse_conn_registry->tls_slot_ == nullptr);
  ENVOY_LOG_MISC(debug, "ReverseConnExtension: creating reverse connection tls slot");
  global_tls_registry_->tls_slot_ =
      ThreadLocal::TypedSlot<Bootstrap::ReverseConnection::RCThreadLocalRegistry>::makeUnique(
          server_context_.threadLocal());
  Stats::Scope& scope = server_context_.scope();
  Upstream::ClusterManager& cluster_manager = server_context_.clusterManager();
  global_tls_registry_->tls_slot_->set([this, &scope, &cluster_manager](Event::Dispatcher& dispatcher) {
    return std::make_shared<Bootstrap::ReverseConnection::RCThreadLocalRegistry>(dispatcher, scope,
                                                                                 stat_prefix_, cluster_manager);
  });
}

Server::BootstrapExtensionPtr ReverseConnExtensionFactory::createBootstrapExtension(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context) {
  ENVOY_LOG_MISC(debug,
                 "ReverseConnListenerFactory: creating reverse conn extension with config: {}",
                 config.DebugString());
  const auto& message = MessageUtil::downcastAndValidate<
      const envoy::extensions::bootstrap::reverse_connection::v3alpha::ReverseConnection&>(
      config, context.messageValidationVisitor());
  return std::make_unique<ReverseConnExtension>(context, message);
}

REGISTER_FACTORY(ReverseConnExtensionFactory, Server::Configuration::BootstrapExtensionFactory);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
