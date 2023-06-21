#pragma once

#include "envoy/server/transport_socket_config.h"
#include "envoy/stats/scope.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Implementation of TransportSocketFactoryContext.
 */
class TransportSocketFactoryContextImpl : public TransportSocketFactoryContext {
public:
  TransportSocketFactoryContextImpl(Server::Configuration::ServerFactoryContext& server_context,
                                    Ssl::ContextManager& context_manager, Stats::Scope& stats_scope,
                                    Upstream::ClusterManager& cm,
                                    ProtobufMessage::ValidationVisitor& validation_visitor)
      : server_context_(server_context), context_manager_(context_manager),
        stats_scope_(stats_scope), cluster_manager_(cm), validation_visitor_(validation_visitor) {}

  /**
   * Pass an init manager to register dynamic secret provider.
   * @param init_manager instance of init manager.
   */
  void setInitManager(Init::Manager& init_manager) { init_manager_ = &init_manager; }

  // TransportSocketFactoryContext
  ServerFactoryContext& serverFactoryContext() override { return server_context_; }
  Upstream::ClusterManager& clusterManager() override { return cluster_manager_; }
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return validation_visitor_;
  }
  Ssl::ContextManager& sslContextManager() override { return context_manager_; }
  Stats::Scope& statsScope() override { return stats_scope_; }
  Secret::SecretManager& secretManager() override {
    return clusterManager().clusterManagerFactory().secretManager();
  }
  Init::Manager& initManager() override {
    ASSERT(init_manager_ != nullptr);
    return *init_manager_;
  }

private:
  Server::Configuration::ServerFactoryContext& server_context_;
  Ssl::ContextManager& context_manager_;
  Stats::Scope& stats_scope_;
  Upstream::ClusterManager& cluster_manager_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;

  Init::Manager* init_manager_{};
};

using TransportSocketFactoryContextImplPtr = std::unique_ptr<TransportSocketFactoryContextImpl>;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
