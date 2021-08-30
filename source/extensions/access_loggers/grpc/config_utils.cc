#include "source/extensions/access_loggers/grpc/config_utils.h"

#include "envoy/singleton/manager.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {

// Singleton registration via macro defined in envoy/singleton/manager.h
SINGLETON_MANAGER_REGISTRATION(grpc_access_logger_cache);

GrpcCommon::GrpcAccessLoggerCacheSharedPtr
getGrpcAccessLoggerCacheSingleton(Server::Configuration::CommonFactoryContext& context) {
  return context.singletonManager().getTyped<GrpcCommon::GrpcAccessLoggerCacheImpl>(
      SINGLETON_MANAGER_REGISTERED_NAME(grpc_access_logger_cache), [&context] {
        return std::make_shared<GrpcCommon::GrpcAccessLoggerCacheImpl>(
            context.clusterManager().grpcAsyncClientManager(), context.scope(),
            context.threadLocal(), context.localInfo());
      });
}

void checkGrpcCluster(const envoy::config::core::v3::GrpcService& config,
                      Envoy::Upstream::ClusterManager::ClusterInfoMaps all_clusters) {
  if (config.has_google_grpc()) {
    return;
  }
  const std::string& cluster_name = config.envoy_grpc().cluster_name();
  const auto& it = all_clusters.active_clusters_.find(cluster_name);
  if (it == all_clusters.active_clusters_.end()) {
    throw EnvoyException(fmt::format("Unknown gRPC client cluster '{}'", cluster_name));
  }
  if (it->second.get().info()->addedViaApi()) {
    throw EnvoyException(fmt::format("gRPC client cluster '{}' is not static", cluster_name));
  }
}

} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
