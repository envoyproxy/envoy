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
  TransportSocketFactoryContextImpl(Server::Admin& admin, Ssl::ContextManager& context_manager,
                                    Stats::Scope& stats_scope, Upstream::ClusterManager& cm,
                                    const LocalInfo::LocalInfo& local_info,
                                    Event::Dispatcher& dispatcher,
                                    Envoy::Runtime::RandomGenerator& random, Stats::Store& stats,
                                    Singleton::Manager& singleton_manager,
                                    ThreadLocal::SlotAllocator& tls)
      : admin_(admin), context_manager_(context_manager), stats_scope_(stats_scope),
        cluster_manager_(cm), local_info_(local_info), dispatcher_(dispatcher), random_(random),
        stats_(stats), singleton_manager_(singleton_manager), tls_(tls) {}

  // TransportSocketFactoryContext
  Server::Admin& admin() override { return admin_; }
  Ssl::ContextManager& sslContextManager() override { return context_manager_; }
  Stats::Scope& statsScope() const override { return stats_scope_; }
  Secret::SecretManager& secretManager() override {
    return cluster_manager_.clusterManagerFactory().secretManager();
  }
  Upstream::ClusterManager& clusterManager() override { return cluster_manager_; }
  const LocalInfo::LocalInfo& localInfo() override { return local_info_; }
  Event::Dispatcher& dispatcher() override { return dispatcher_; }
  Envoy::Runtime::RandomGenerator& random() override { return random_; }
  Stats::Store& stats() override { return stats_; }
  void setInitManager(Init::Manager& init_manager) override { init_manager_ = &init_manager; }
  Init::Manager* initManager() override { return init_manager_; }
  Singleton::Manager& singletonManager() override { return singleton_manager_; }
  ThreadLocal::SlotAllocator& threadLocal() override { return tls_; }

private:
  Server::Admin& admin_;
  Ssl::ContextManager& context_manager_;
  Stats::Scope& stats_scope_;
  Upstream::ClusterManager& cluster_manager_;
  const LocalInfo::LocalInfo& local_info_;
  Event::Dispatcher& dispatcher_;
  Envoy::Runtime::RandomGenerator& random_;
  Stats::Store& stats_;
  Singleton::Manager& singleton_manager_;
  ThreadLocal::SlotAllocator& tls_;
  Init::Manager* init_manager_{};
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
