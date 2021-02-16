#include "extensions/access_loggers/open_telemetry/config.h"

#include "envoy/extensions/access_loggers/open_telemetry/v3/open_telemetry.pb.h"
#include "envoy/extensions/access_loggers/open_telemetry/v3/open_telemetry.pb.validate.h"

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/access_log_config.h"

#include "common/common/assert.h"
#include "common/common/macros.h"
#include "common/grpc/async_client_impl.h"
#include "common/protobuf/protobuf.h"

#include "extensions/access_loggers/open_telemetry/access_log_proto_descriptors.h"
#include "extensions/access_loggers/open_telemetry/access_log_impl.h"
#include "extensions/access_loggers/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {

// Singleton registration via macro defined in envoy/singleton/manager.h
SINGLETON_MANAGER_REGISTRATION(open_telemetry_access_logger_cache);

GrpcAccessLoggerCacheSharedPtr
getAccessLoggerCacheSingleton(Server::Configuration::FactoryContext& context) {
  return context.singletonManager().getTyped<GrpcAccessLoggerCacheImpl>(
      SINGLETON_MANAGER_REGISTERED_NAME(open_telemetry_access_logger_cache), [&context] {
        return std::make_shared<GrpcAccessLoggerCacheImpl>(
            context.clusterManager().grpcAsyncClientManager(), context.scope(),
            context.threadLocal(), context.localInfo());
      });
}

::Envoy::AccessLog::InstanceSharedPtr
createAccessLogInstance(const Protobuf::Message& config, ::Envoy::AccessLog::FilterPtr&& filter,
                        Server::Configuration::FactoryContext& context) {
  validateProtoDescriptors();

  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&>(
      config, context.messageValidationVisitor());

  return std::make_shared<AccessLog>(std::move(filter), proto_config, context.threadLocal(),
                                     getAccessLoggerCacheSingleton(context), context.scope());
}

ProtobufTypes::MessagePtr AccessLogFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig>();
}

std::string AccessLogFactory::name() const { return AccessLogNames::get().OpenTelemetry; }

/**
 * Static registration for the OpenTelemetry (gRPC) access log. @see RegisterFactory.
 */
REGISTER_FACTORY(AccessLogFactory, Server::Configuration::AccessLogInstanceFactory){
    "envoy.open_telemetry_access_log"};

} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
