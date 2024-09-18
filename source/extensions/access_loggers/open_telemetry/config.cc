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

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {

// Singleton registration via macro defined in envoy/singleton/manager.h
SINGLETON_MANAGER_REGISTRATION(open_telemetry_access_logger_cache);

GrpcAccessLoggerCacheSharedPtr
getAccessLoggerCacheSingleton(Server::Configuration::CommonFactoryContext& context) {
  return context.singletonManager().getTyped<GrpcAccessLoggerCacheImpl>(
      SINGLETON_MANAGER_REGISTERED_NAME(open_telemetry_access_logger_cache), [&context] {
        return std::make_shared<GrpcAccessLoggerCacheImpl>(
            context.clusterManager().grpcAsyncClientManager(), context.serverScope(),
            context.threadLocal(), context.localInfo());
      });
}

::Envoy::AccessLog::InstanceSharedPtr
AccessLogFactory::createAccessLogInstance(const Protobuf::Message& config,
                                          ::Envoy::AccessLog::FilterPtr&& filter,
                                          Server::Configuration::FactoryContext& context) {
  validateProtoDescriptors();

  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&>(
      config, context.messageValidationVisitor());

  auto commands = THROW_OR_RETURN_VALUE(
      Formatter::SubstitutionFormatStringUtils::parseFormatters(proto_config.formatters(), context),
      std::vector<Formatter::CommandParserBasePtr<Formatter::HttpFormatterContext>>);

  return std::make_shared<AccessLog>(
      std::move(filter), proto_config, context.serverFactoryContext().threadLocal(),
      getAccessLoggerCacheSingleton(context.serverFactoryContext()), commands);
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
