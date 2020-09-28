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
                                    Event::Dispatcher& dispatcher, Stats::Store& stats,
                                    Singleton::Manager& singleton_manager,
                                    ThreadLocal::SlotAllocator& tls,
                                    ProtobufMessage::ValidationVisitor& validation_visitor,
                                    Api::Api& api)
      : admin_(admin), context_manager_(context_manager), stats_scope_(stats_scope),
        cluster_manager_(cm), local_info_(local_info), dispatcher_(dispatcher), stats_(stats),
        singleton_manager_(singleton_manager), tls_(tls), validation_visitor_(validation_visitor),
        api_(api) {}

  /**
   * Pass an init manager to register dynamic secret provider.
   * @param init_manager instance of init manager.
   */
  void setInitManager(Init::Manager& init_manager) { init_manager_ = &init_manager; }

  // TransportSocketFactoryContext
  Server::Admin& admin() override { return admin_; }
  Ssl::ContextManager& sslContextManager() override { return context_manager_; }
  Stats::Scope& scope() override { return stats_scope_; }
  Secret::SecretManager& secretManager() override {
    return cluster_manager_.clusterManagerFactory().secretManager();
  }
  Upstream::ClusterManager& clusterManager() override { return cluster_manager_; }
  const LocalInfo::LocalInfo& localInfo() const override { return local_info_; }
  Event::Dispatcher& dispatcher() override { return dispatcher_; }
  Stats::Store& stats() override { return stats_; }
  Init::Manager& initManager() override {
    ASSERT(init_manager_ != nullptr);
    return *init_manager_;
  }
  Singleton::Manager& singletonManager() override { return singleton_manager_; }
  ThreadLocal::SlotAllocator& threadLocal() override { return tls_; }
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return validation_visitor_;
  }
  Api::Api& api() override { return api_; }

private:
  Server::Admin& admin_;
  Ssl::ContextManager& context_manager_;
  Stats::Scope& stats_scope_;
  Upstream::ClusterManager& cluster_manager_;
  const LocalInfo::LocalInfo& local_info_;
  Event::Dispatcher& dispatcher_;
  Stats::Store& stats_;
  Singleton::Manager& singleton_manager_;
  ThreadLocal::SlotAllocator& tls_;
  Init::Manager* init_manager_{};
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  Api::Api& api_;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
