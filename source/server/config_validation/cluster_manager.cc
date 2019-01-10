#include "server/config_validation/cluster_manager.h"

#include "common/common/utility.h"

namespace Envoy {
namespace Upstream {

ValidationClusterManagerFactory::ValidationClusterManagerFactory(
    Runtime::Loader& runtime, Stats::Store& stats, ThreadLocal::Instance& tls,
    Runtime::RandomGenerator& random, Network::DnsResolverSharedPtr dns_resolver,
    Ssl::ContextManager& ssl_context_manager, Event::Dispatcher& main_thread_dispatcher,
    const LocalInfo::LocalInfo& local_info, Secret::SecretManager& secret_manager, Api::Api& api,
    Http::Context& http_context)
    : ProdClusterManagerFactory(runtime, stats, tls, random, dns_resolver, ssl_context_manager,
                                main_thread_dispatcher, local_info, secret_manager, api,
                                http_context) {}

ClusterManagerPtr ValidationClusterManagerFactory::clusterManagerFromProto(
    const envoy::config::bootstrap::v2::Bootstrap& bootstrap, Stats::Store& stats,
    ThreadLocal::Instance& tls, Runtime::Loader& runtime, Runtime::RandomGenerator& random,
    const LocalInfo::LocalInfo& local_info, AccessLog::AccessLogManager& log_manager,
    Server::Admin& admin) {
  return std::make_unique<ValidationClusterManager>(
      bootstrap, *this, stats, tls, runtime, random, local_info, log_manager,
      main_thread_dispatcher_, admin, api_, http_context_);
}

CdsApiPtr
ValidationClusterManagerFactory::createCds(const envoy::api::v2::core::ConfigSource& cds_config,
                                           ClusterManager& cm) {
  // Create the CdsApiImpl...
  ProdClusterManagerFactory::createCds(cds_config, cm);
  // ... and then throw it away, so that we don't actually connect to it.
  return nullptr;
}

ValidationClusterManager::ValidationClusterManager(
    const envoy::config::bootstrap::v2::Bootstrap& bootstrap, ClusterManagerFactory& factory,
    Stats::Store& stats, ThreadLocal::Instance& tls, Runtime::Loader& runtime,
    Runtime::RandomGenerator& random, const LocalInfo::LocalInfo& local_info,
    AccessLog::AccessLogManager& log_manager, Event::Dispatcher& main_thread_dispatcher,
    Server::Admin& admin, Api::Api& api, Http::Context& http_context)
    : ClusterManagerImpl(bootstrap, factory, stats, tls, runtime, random, local_info, log_manager,
                         main_thread_dispatcher, admin, api, http_context),
      async_client_(main_thread_dispatcher.timeSystem(), api) {}

Http::ConnectionPool::Instance*
ValidationClusterManager::httpConnPoolForCluster(const std::string&, ResourcePriority,
                                                 Http::Protocol, LoadBalancerContext*) {
  return nullptr;
}

Host::CreateConnectionData
ValidationClusterManager::tcpConnForCluster(const std::string&, LoadBalancerContext*,
                                            Network::TransportSocketOptionsSharedPtr) {
  return Host::CreateConnectionData{nullptr, nullptr};
}

Http::AsyncClient& ValidationClusterManager::httpAsyncClientForCluster(const std::string&) {
  return async_client_;
}

} // namespace Upstream
} // namespace Envoy
