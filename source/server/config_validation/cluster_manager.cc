#include "server/config_validation/cluster_manager.h"

#include "common/common/utility.h"

namespace Envoy {
namespace Upstream {

ClusterManagerPtr ValidationClusterManagerFactory::clusterManagerFromProto(
    const envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
  return std::make_unique<ValidationClusterManager>(
      bootstrap, *this, stats_, tls_, runtime_, random_, local_info_, log_manager_,
      main_thread_dispatcher_, admin_, validation_context_, api_, http_context_, time_system_);
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
    Server::Admin& admin, ProtobufMessage::ValidationContext& validation_context, Api::Api& api,
    Http::Context& http_context, Event::TimeSystem& time_system)
    : ClusterManagerImpl(bootstrap, factory, stats, tls, runtime, random, local_info, log_manager,
                         main_thread_dispatcher, admin, validation_context, api, http_context),
      async_client_(api, time_system) {}

Http::ConnectionPool::Instance*
ValidationClusterManager::httpConnPoolForCluster(const std::string&, ResourcePriority,
                                                 Http::Protocol, LoadBalancerContext*) {
  return nullptr;
}

Host::CreateConnectionData ValidationClusterManager::tcpConnForCluster(const std::string&,
                                                                       LoadBalancerContext*) {
  return Host::CreateConnectionData{nullptr, nullptr};
}

Http::AsyncClient& ValidationClusterManager::httpAsyncClientForCluster(const std::string&) {
  return async_client_;
}

} // namespace Upstream
} // namespace Envoy
