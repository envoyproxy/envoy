#include "extensions/access_loggers/grpc/tcp_config.h"

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
#include "extensions/access_loggers/grpc/tcp_grpc_access_log_impl.h"
#include "extensions/access_loggers/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace TcpGrpc {

AccessLog::InstanceSharedPtr
TcpGrpcAccessLogFactory::createAccessLogInstance(const Protobuf::Message& config,
                                                 AccessLog::FilterPtr&& filter,
                                                 Server::Configuration::FactoryContext& context) {
  GrpcCommon::validateProtoDescriptors();

  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::access_loggers::grpc::v3::TcpGrpcAccessLogConfig&>(
      config, context.messageValidationVisitor());

  return std::make_shared<TcpGrpcAccessLog>(std::move(filter), proto_config, context.threadLocal(),
                                            GrpcCommon::getGrpcAccessLoggerCacheSingleton(context));
}

ProtobufTypes::MessagePtr TcpGrpcAccessLogFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::access_loggers::grpc::v3::TcpGrpcAccessLogConfig>();
}

std::string TcpGrpcAccessLogFactory::name() const { return AccessLogNames::get().TcpGrpc; }

/**
 * Static registration for the TCP gRPC access log. @see RegisterFactory.
 */
REGISTER_FACTORY(TcpGrpcAccessLogFactory,
                 Server::Configuration::AccessLogInstanceFactory){"envoy.tcp_grpc_access_log"};

} // namespace TcpGrpc
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
