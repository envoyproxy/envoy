#include "source/extensions/access_loggers/open_telemetry/config.h"

#include "envoy/extensions/access_loggers/open_telemetry/v3/logs_service.pb.h"
#include "envoy/extensions/access_loggers/open_telemetry/v3/logs_service.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"
#include "source/common/formatter/substitution_format_string.h"
#include "source/common/grpc/async_client_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/access_loggers/open_telemetry/access_log_impl.h"
#include "source/extensions/access_loggers/open_telemetry/access_log_proto_descriptors.h"
#include "source/extensions/access_loggers/open_telemetry/http_access_log_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {

// Singleton registration via macro defined in envoy/singleton/manager.h
SINGLETON_MANAGER_REGISTRATION(open_telemetry_access_logger_cache);
SINGLETON_MANAGER_REGISTRATION(open_telemetry_http_access_logger_cache);

std::shared_ptr<GrpcAccessLoggerCacheImpl>
getGrpcAccessLoggerCacheSingleton(Server::Configuration::CommonFactoryContext& context) {
  return context.singletonManager().getTyped<GrpcAccessLoggerCacheImpl>(
      SINGLETON_MANAGER_REGISTERED_NAME(open_telemetry_access_logger_cache), [&context] {
        return std::make_shared<GrpcAccessLoggerCacheImpl>(
            context.clusterManager().grpcAsyncClientManager(), context.serverScope(),
            context.threadLocal(), context.localInfo());
      });
}

HttpAccessLoggerCacheSharedPtr
getHttpAccessLoggerCacheSingleton(Server::Configuration::CommonFactoryContext& context) {
  return context.singletonManager().getTyped<HttpAccessLoggerCacheImpl>(
      SINGLETON_MANAGER_REGISTERED_NAME(open_telemetry_http_access_logger_cache), [&context] {
        return std::make_shared<HttpAccessLoggerCacheImpl>(
            context.clusterManager(), context.serverScope(), context.threadLocal(),
            context.localInfo());
      });
}

::Envoy::AccessLog::InstanceSharedPtr AccessLogFactory::createAccessLogInstance(
    const Protobuf::Message& config, ::Envoy::AccessLog::FilterPtr&& filter,
    Server::Configuration::GenericFactoryContext& context,
    std::vector<Formatter::CommandParserPtr>&& command_parsers) {
  validateProtoDescriptors();

  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&>(
      config, context.messageValidationVisitor());

  // Validate transport configuration: exactly one transport must be specified.
  const bool has_grpc_service = proto_config.has_grpc_service();
  const bool has_http_service = proto_config.has_http_service();
  const bool has_common_config_grpc =
      proto_config.has_common_config() && proto_config.common_config().has_grpc_service();

  const int transport_count =
      (has_grpc_service ? 1 : 0) + (has_http_service ? 1 : 0) + (has_common_config_grpc ? 1 : 0);

  if (transport_count == 0) {
    throw EnvoyException(
        "OpenTelemetry access logger requires one of: grpc_service, http_service, or "
        "common_config.grpc_service to be configured.");
  }

  if (transport_count > 1) {
    throw EnvoyException(
        "OpenTelemetry access logger can only have one transport configured. "
        "Specify exactly one of: grpc_service, http_service, or common_config.grpc_service.");
  }

  auto commands =
      THROW_OR_RETURN_VALUE(Formatter::SubstitutionFormatStringUtils::parseFormatters(
                                proto_config.formatters(), context, std::move(command_parsers)),
                            std::vector<Formatter::CommandParserPtr>);

  // Create appropriate access log based on transport type.
  if (has_http_service) {
    return std::make_shared<HttpAccessLog>(
        std::move(filter), proto_config, context.serverFactoryContext().threadLocal(),
        getHttpAccessLoggerCacheSingleton(context.serverFactoryContext()), commands);
  }

  return std::make_shared<AccessLog>(
      std::move(filter), proto_config, context.serverFactoryContext().threadLocal(),
      getGrpcAccessLoggerCacheSingleton(context.serverFactoryContext()), commands);
}

ProtobufTypes::MessagePtr AccessLogFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig>();
}

std::string AccessLogFactory::name() const { return "envoy.access_loggers.open_telemetry"; }

/**
 * Static registration for the OpenTelemetry (gRPC) access log. @see RegisterFactory.
 */
REGISTER_FACTORY(AccessLogFactory,
                 Envoy::AccessLog::AccessLogInstanceFactory){"envoy.open_telemetry_access_log"};

} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
