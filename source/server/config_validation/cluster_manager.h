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
      Server::Configuration::ServerFactoryContext& server_context, Stats::Store& stats,
      ThreadLocal::Instance& tls, Http::Context& http_context,
      LazyCreateDnsResolver dns_resolver_fn, Ssl::ContextManager& ssl_context_manager,
      Secret::SecretManager& secret_manager, Quic::QuicStatNames& quic_stat_names,
      Server::Instance& server)
      : ProdClusterManagerFactory(server_context, stats, tls, http_context, dns_resolver_fn,
                                  ssl_context_manager, secret_manager, quic_stat_names, server) {}

  ClusterManagerPtr
  clusterManagerFromProto(const envoy::config::bootstrap::v3::Bootstrap& bootstrap) override;

  // Delegates to ProdClusterManagerFactory::createCds, but discards the result and returns nullptr
  // unconditionally.
  CdsApiPtr createCds(const envoy::config::core::v3::ConfigSource& cds_config,
                      const xds::core::v3::ResourceLocator* cds_resources_locator,
                      ClusterManager& cm) override;
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

  // Gives access to the protected constructor.
  friend ValidationClusterManagerFactory;
};

} // namespace Upstream
} // namespace Envoy
