#include "source/extensions/access_loggers/grpc/http_config.h"

#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"
#include "source/common/grpc/async_client_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/access_loggers/grpc/config_utils.h"
#include "source/extensions/access_loggers/grpc/grpc_access_log_proto_descriptors.h"
#include "source/extensions/access_loggers/grpc/http_grpc_access_log_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace HttpGrpc {

AccessLog::InstanceSharedPtr HttpGrpcAccessLogFactory::createAccessLogInstance(
    const Protobuf::Message& config, AccessLog::FilterPtr&& filter,
    Server::Configuration::ListenerAccessLogFactoryContext& context) {
  return createAccessLogInstance(
      config, std::move(filter),
      static_cast<Server::Configuration::CommonFactoryContext&>(context));
}

AccessLog::InstanceSharedPtr HttpGrpcAccessLogFactory::createAccessLogInstance(
    const Protobuf::Message& config, AccessLog::FilterPtr&& filter,
    Server::Configuration::CommonFactoryContext& context) {
  GrpcCommon::validateProtoDescriptors();

  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::access_loggers::grpc::v3::HttpGrpcAccessLogConfig&>(
      config, context.messageValidationVisitor());

  return std::make_shared<HttpGrpcAccessLog>(
      std::move(filter), proto_config, context.threadLocal(),
      GrpcCommon::getGrpcAccessLoggerCacheSingleton(context));
}

ProtobufTypes::MessagePtr HttpGrpcAccessLogFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::access_loggers::grpc::v3::HttpGrpcAccessLogConfig>();
}

std::string HttpGrpcAccessLogFactory::name() const { return "envoy.access_loggers.http_grpc"; }

/**
 * Static registration for the HTTP gRPC access log. @see RegisterFactory.
 */
REGISTER_FACTORY(HttpGrpcAccessLogFactory,
                 Server::Configuration::AccessLogInstanceFactory){"envoy.http_grpc_access_log"};

} // namespace HttpGrpc
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
