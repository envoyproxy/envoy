#include "extensions/access_loggers/grpc/http_config.h"

#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/common/assert.h"
#include "common/common/macros.h"
#include "common/grpc/async_client_impl.h"
#include "common/protobuf/protobuf.h"

#include "extensions/access_loggers/grpc/config_utils.h"
#include "extensions/access_loggers/grpc/grpc_access_log_proto_descriptors.h"
#include "extensions/access_loggers/grpc/http_grpc_access_log_impl.h"
#include "extensions/access_loggers/grpc/ot_grpc_access_log_impl.h"
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

AccessLog::InstanceSharedPtr
OpenTelemetry::createAccessLogInstance(const Protobuf::Message& config,
                                       AccessLog::FilterPtr&& filter,
                                       Server::Configuration::FactoryContext& context) {
  // GrpcCommon::validateProtoDescriptors();

  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::access_loggers::grpc::v3::OpenTelemetryAccessLogConfig&>(
      config, context.messageValidationVisitor());

  return std::make_shared<OpenTelemetry::AccessLog>(
      std::move(filter), proto_config, context.threadLocal(),
      getAccessLoggerCacheSingleton(context), context.scope());
}

ProtobufTypes::MessagePtr HttpGrpcAccessLogFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::access_loggers::grpc::v3::HttpGrpcAccessLogConfig>();
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
