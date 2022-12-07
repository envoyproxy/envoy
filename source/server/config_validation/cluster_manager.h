#pragma once

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/server/options.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/http/context_impl.h"
#include "source/common/upstream/cluster_manager_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * Config-validation-only implementation of ClusterManagerFactory, which creates
 * ValidationClusterManagers. It also creates, but never returns, CdsApiImpls.
 */
class ValidationClusterManagerFactory : public ProdClusterManagerFactory {
public:
  using ProdClusterManagerFactory::ProdClusterManagerFactory;

  explicit ValidationClusterManagerFactory(
      OptRef<Server::Admin> admin, Runtime::Loader& runtime, Stats::Store& stats,
      ThreadLocal::Instance& tls, LazyCreateDnsResolver dns_resolver_fn,
      Ssl::ContextManager& ssl_context_manager, Event::Dispatcher& main_thread_dispatcher,
      const LocalInfo::LocalInfo& local_info, Secret::SecretManager& secret_manager,
      ProtobufMessage::ValidationContext& validation_context, Api::Api& api,
      Http::Context& http_context, Grpc::Context& grpc_context, Router::Context& router_context,
      AccessLog::AccessLogManager& log_manager, Singleton::Manager& singleton_manager,
      const Server::Options& options, Quic::QuicStatNames& quic_stat_names,
      Server::Instance& server)
      : ProdClusterManagerFactory(
            const_cast<Server::Configuration::ServerFactoryContext&>(server.serverFactoryContext()),
            admin, runtime, stats, tls, dns_resolver_fn, ssl_context_manager,
            main_thread_dispatcher, local_info, secret_manager, validation_context, api,
            http_context, grpc_context, router_context, log_manager, singleton_manager, options,
            quic_stat_names, server),
        grpc_context_(grpc_context), router_context_(router_context) {}

  ClusterManagerPtr
  clusterManagerFromProto(const envoy::config::bootstrap::v3::Bootstrap& bootstrap) override;

  // Delegates to ProdClusterManagerFactory::createCds, but discards the result and returns nullptr
  // unconditionally.
  CdsApiPtr createCds(const envoy::config::core::v3::ConfigSource& cds_config,
                      const xds::core::v3::ResourceLocator* cds_resources_locator,
                      ClusterManager& cm) override;

private:
  Grpc::Context& grpc_context_;
  Router::Context& router_context_;
};

/**
 * Config-validation-only implementation of ClusterManager, which opens no upstream connections.
 */
class ValidationClusterManager : public ClusterManagerImpl {
public:
  using ClusterManagerImpl::ClusterManagerImpl;

  ThreadLocalCluster* getThreadLocalCluster(absl::string_view) override {
    // A thread local cluster is never guaranteed to exist (because it is not created until warmed)
    // so all calling code must have error handling for this case. Returning nullptr here prevents
    // any calling code creating real outbound networking during validation.
    return nullptr;
  }
};

} // namespace Upstream
} // namespace Envoy
