#include "server/config_validation/cluster_manager.h"

namespace Envoy {
namespace Upstream {

ValidationClusterManagerFactory::ValidationClusterManagerFactory(
    Runtime::Loader& runtime, Stats::Store& stats, ThreadLocal::Instance& tls,
    Runtime::RandomGenerator& random, Network::DnsResolver& dns_resolver,
    Ssl::ContextManager& ssl_context_manager, Event::Dispatcher& primary_dispatcher,
    const LocalInfo::LocalInfo& local_info)
    : ProdClusterManagerFactory(runtime, stats, tls, random, dns_resolver, ssl_context_manager,
                                primary_dispatcher, local_info) {}

ClusterManagerPtr ValidationClusterManagerFactory::clusterManagerFromJson(
    const Json::Object& config, Stats::Store& stats, ThreadLocal::Instance& tls,
    Runtime::Loader& runtime, Runtime::RandomGenerator& random,
    const LocalInfo::LocalInfo& local_info, AccessLog::AccessLogManager& log_manager) {
  return ClusterManagerPtr{new ValidationClusterManager(config, *this, stats, tls, runtime, random,
                                                        local_info, log_manager)};
}

CdsApiPtr ValidationClusterManagerFactory::createCds(const Json::Object& config,
                                                     ClusterManager& cm) {
  // Create the CdsApiImpl...
  CdsApiPtr cds = ProdClusterManagerFactory::createCds(config, cm);
  // ... and then throw it away, so that we don't actually connect to it.
  return nullptr;
}

ValidationClusterManager::ValidationClusterManager(
    const Json::Object& config, ClusterManagerFactory& factory, Stats::Store& stats,
    ThreadLocal::Instance& tls, Runtime::Loader& runtime, Runtime::RandomGenerator& random,
    const LocalInfo::LocalInfo& local_info, AccessLog::AccessLogManager& log_manager)
    : ClusterManagerImpl(config, factory, stats, tls, runtime, random, local_info, log_manager) {}

Http::ConnectionPool::Instance*
ValidationClusterManager::httpConnPoolForCluster(const std::string&, ResourcePriority,
                                                 LoadBalancerContext*) {
  return nullptr;
}

Host::CreateConnectionData ValidationClusterManager::tcpConnForCluster(const std::string&) {
  return Host::CreateConnectionData{nullptr, nullptr};
}

Http::AsyncClient& ValidationClusterManager::httpAsyncClientForCluster(const std::string&) {
  return async_client_;
}

} // Upstream
} // Envoy
