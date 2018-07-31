#pragma once

#include "envoy/server/transport_socket_config.h"

#include "common/secret/dynamic_secret_provider_factory_impl.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Implementation of TransportSocketFactoryContext.
 */
class TransportSocketFactoryContextImpl : public TransportSocketFactoryContext {
public:
  TransportSocketFactoryContextImpl(
      Ssl::ContextManager& context_manager, Stats::Scope& stats_scope, Upstream::ClusterManager& cm,
      Init::Manager& init_manager,
      Secret::DynamicTlsCertificateSecretProviderFactoryContext& secret_provider_context)
      : context_manager_(context_manager), stats_scope_(stats_scope), cluster_manager_(cm),
        init_manager_(init_manager),
        secret_provider_factory_(secret_provider_context, init_manager_) {}

  Ssl::ContextManager& sslContextManager() override { return context_manager_; }

  Stats::Scope& statsScope() const override { return stats_scope_; }

  Init::Manager& initManager() override { return init_manager_; }

  Upstream::ClusterManager& clusterManager() override { return cluster_manager_; }

  Secret::SecretManager& secretManager() override {
    return cluster_manager_.clusterManagerFactory().secretManager();
  }

  Secret::DynamicTlsCertificateSecretProviderFactory&
  dynamicTlsCertificateSecretProviderFactory() override {
    return secret_provider_factory_;
  }

private:
  Ssl::ContextManager& context_manager_;
  Stats::Scope& stats_scope_;
  Upstream::ClusterManager& cluster_manager_;
  Init::Manager& init_manager_;
  Secret::DynamicTlsCertificateSecretProviderFactoryImpl secret_provider_factory_;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy