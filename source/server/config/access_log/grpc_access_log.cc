#include "server/config/access_log/grpc_access_log.h"

#include "envoy/config/accesslog/v2/als.pb.validate.h"
#include "envoy/config/filter/accesslog/v2/accesslog.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/access_log/grpc_access_log_impl.h"
#include "common/common/macros.h"
#include "common/config/well_known_names.h"
#include "common/grpc/async_client_impl.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {
namespace Configuration {

// Singleton registration via macro defined in envoy/singleton/manager.h
SINGLETON_MANAGER_REGISTRATION(grpc_access_log_streamer);

AccessLog::InstanceSharedPtr HttpGrpcAccessLogFactory::createAccessLogInstance(
    const Protobuf::Message& config, AccessLog::FilterPtr&& filter, FactoryContext& context) {
  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::config::accesslog::v2::HttpGrpcAccessLogConfig&>(config);
  std::shared_ptr<AccessLog::GrpcAccessLogStreamer> grpc_access_log_streamer =
      context.singletonManager().getTyped<AccessLog::GrpcAccessLogStreamer>(
          SINGLETON_MANAGER_REGISTERED_NAME(grpc_access_log_streamer),
          [&context, grpc_service = proto_config.common_config().grpc_service() ] {
            return std::make_shared<AccessLog::GrpcAccessLogStreamerImpl>(
                context.clusterManager().grpcAsyncClientManager().factoryForGrpcService(
                    grpc_service, context.scope(), false),
                context.threadLocal(), context.localInfo());
          });

  return AccessLog::InstanceSharedPtr{
      new AccessLog::HttpGrpcAccessLog(std::move(filter), proto_config, grpc_access_log_streamer)};
}

ProtobufTypes::MessagePtr HttpGrpcAccessLogFactory::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{new envoy::config::accesslog::v2::HttpGrpcAccessLogConfig()};
}

std::string HttpGrpcAccessLogFactory::name() const {
  return Config::AccessLogNames::get().HTTP_GRPC;
}

/**
 * Static registration for the HTTP gRPC access log. @see RegisterFactory.
 */
static Registry::RegisterFactory<HttpGrpcAccessLogFactory, AccessLogInstanceFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
