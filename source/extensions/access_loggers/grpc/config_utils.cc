#include "extensions/access_loggers/grpc/config_utils.h"

#include "envoy/singleton/manager.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {

// Singleton registration via macro defined in envoy/singleton/manager.h
SINGLETON_MANAGER_REGISTRATION(grpc_access_logger_cache);

GrpcCommon::GrpcAccessLoggerCacheSharedPtr
getGrpcAccessLoggerCacheSingleton(Server::Configuration::FactoryContext& context) {
  return context.singletonManager().getTyped<GrpcCommon::GrpcAccessLoggerCache>(
      SINGLETON_MANAGER_REGISTERED_NAME(grpc_access_logger_cache), [&context] {
        return std::make_shared<GrpcCommon::GrpcAccessLoggerCacheImpl>(
            context.clusterManager().grpcAsyncClientManager(), context.scope(),
            context.threadLocal(), context.localInfo());
      });
}
} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
