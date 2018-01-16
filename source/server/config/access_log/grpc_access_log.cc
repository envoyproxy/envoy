#include "server/config/access_log/grpc_access_log.h"

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/access_log/grpc_access_log_impl.h"
#include "common/common/macros.h"
#include "common/config/well_known_names.h"
#include "common/grpc/async_client_impl.h"
#include "common/protobuf/protobuf.h"

#include "api/filter/accesslog/accesslog.pb.validate.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class GrpcAccessLogClientFactoryImpl : public AccessLog::GrpcAccessLogClientFactory {
public:
  GrpcAccessLogClientFactoryImpl(Upstream::ClusterManager& cluster_manager,
                                 const std::string& cluster_name)
      : cluster_manager_(cluster_manager), cluster_name_(cluster_name) {}

  // AccessLog::GrpcAccessLogClientFactory
  AccessLog::GrpcAccessLogClientPtr create() override {
    return std::make_unique<
        Grpc::AsyncClientImpl<envoy::api::v2::filter::accesslog::StreamAccessLogsMessage,
                              envoy::api::v2::filter::accesslog::StreamAccessLogsResponse>>(
        cluster_manager_, cluster_name_);
  };

  Upstream::ClusterManager& cluster_manager_;
  const std::string cluster_name_;
};

// Singleton registration via macro defined in envoy/singleton/manager.h
SINGLETON_MANAGER_REGISTRATION(grpc_access_log_streamer);

AccessLog::InstanceSharedPtr HttpGrpcAccessLogFactory::createAccessLogInstance(
    const Protobuf::Message& config, AccessLog::FilterPtr&& filter, FactoryContext& context) {
  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::api::v2::filter::accesslog::HttpGrpcAccessLogConfig&>(config);

  // TODO(htuch): Support Google gRPC client.
  const auto cluster_name = proto_config.common_config().grpc_service().envoy_grpc().cluster_name();
  auto cluster = context.clusterManager().get(cluster_name);
  if (cluster == nullptr || cluster->info()->addedViaApi()) {
    throw EnvoyException(fmt::format(
        "invalid access log cluster '{}'. Missing or not a static cluster.", cluster_name));
  }

  std::shared_ptr<AccessLog::GrpcAccessLogStreamer> grpc_access_log_streamer =
      context.singletonManager().getTyped<AccessLog::GrpcAccessLogStreamer>(
          SINGLETON_MANAGER_REGISTERED_NAME(grpc_access_log_streamer), [&context, cluster_name] {
            return std::make_shared<AccessLog::GrpcAccessLogStreamerImpl>(
                std::make_unique<GrpcAccessLogClientFactoryImpl>(context.clusterManager(),
                                                                 cluster_name),
                context.threadLocal(), context.localInfo());
          });

  return AccessLog::InstanceSharedPtr{
      new AccessLog::HttpGrpcAccessLog(std::move(filter), proto_config, grpc_access_log_streamer)};
}

ProtobufTypes::MessagePtr HttpGrpcAccessLogFactory::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{
      new envoy::api::v2::filter::accesslog::HttpGrpcAccessLogConfig()};
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
