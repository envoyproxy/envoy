#pragma once

#include "envoy/server/filter_config.h"

#include "source/extensions/access_loggers/grpc/grpc_access_log_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {

GrpcAccessLoggerCacheSharedPtr
getGrpcAccessLoggerCacheSingleton(Server::Configuration::CommonFactoryContext& context);

void checkGrpcCluster(const envoy::config::core::v3::GrpcService& config,
                      Envoy::Upstream::ClusterManager::ClusterInfoMaps all_clusters);

} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
