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
        auto* filter_factory_context =
            dynamic_cast<Server::Configuration::FactoryContext*>(&context);
        // Note that the factory context can be server factory context. The life of the scope in
        // server factory context is good.
        FANCY_LOG(trace, "in {} access log cache is created from {}", __FUNCTION__,
                  filter_factory_context != nullptr ? "unsafe filter factory context"
                                                    : "safe server/listener factory context");
        FANCY_LOG(trace, "maybe unsafe scope addr = {} ", static_cast<void*>(&context.scope()));

        auto& scope = filter_factory_context == nullptr
                          ? context.scope()
                          : filter_factory_context->getServerFactoryContext().scope();
        return std::make_shared<GrpcCommon::GrpcAccessLoggerCacheImpl>(
            context.clusterManager().grpcAsyncClientManager(), scope, context.threadLocal(),
            context.localInfo());
      });
}
} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
