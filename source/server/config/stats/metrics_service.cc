#include "server/config/stats/metrics_service.h"

#include <string>

#include "envoy/registry/registry.h"

#include "common/config/well_known_names.h"
#include "common/grpc/async_client_impl.h"
#include "common/network/resolver_impl.h"
#include "common/stats/grpc_metrics_service_impl.h"

#include "api/metrics_service.pb.h"
#include "api/metrics_service.pb.validate.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class GrpcMetricsServiceClientFactoryImpl : public Stats::Metrics::GrpcMetricsServiceClientFactory {
public:
  GrpcMetricsServiceClientFactoryImpl(Upstream::ClusterManager& cluster_manager,
                                      const std::string& cluster_name)
      : cluster_manager_(cluster_manager), cluster_name_(cluster_name) {}

  // Metrics::GrpcMetricsServiceClientPtr
  Stats::Metrics::GrpcMetricsServiceClientPtr create() override {
    return std::make_unique<Grpc::AsyncClientImpl<envoy::api::v2::StreamMetricsMessage,
                                                  envoy::api::v2::StreamMetricsResponse>>(
        cluster_manager_, cluster_name_);
  };

  Upstream::ClusterManager& cluster_manager_;
  const std::string cluster_name_;
};

Stats::SinkPtr MetricsServiceSinkFactory::createStatsSink(const Protobuf::Message& config,
                                                          Server::Instance& server) {
  const auto& sink_config =
      MessageUtil::downcastAndValidate<const envoy::api::v2::MetricsServiceConfig&>(config);
  const std::string cluster_name = sink_config.grpc_service().envoy_grpc().cluster_name();
  ENVOY_LOG(debug, "Metrics Service cluster name: {}", cluster_name);

  std::shared_ptr<Stats::Metrics::GrpcMetricsStreamer> grpc_metrics_streamer =
      std::make_shared<Stats::Metrics::GrpcMetricsStreamerImpl>(
          std::make_unique<GrpcMetricsServiceClientFactoryImpl>(server.clusterManager(),
                                                                cluster_name),
          server.threadLocal(), server.localInfo());

  return Stats::SinkPtr(
      std::make_unique<Stats::Metrics::MetricsServiceSink>(grpc_metrics_streamer));
}

ProtobufTypes::MessagePtr MetricsServiceSinkFactory::createEmptyConfigProto() {
  return std::unique_ptr<envoy::api::v2::MetricsServiceConfig>(
      std::make_unique<envoy::api::v2::MetricsServiceConfig>());
}

std::string MetricsServiceSinkFactory::name() {
  return Config::StatsSinkNames::get().METRICS_SERVICE;
}

/**
 * Static registration for the this sink factory. @see RegisterFactory.
 */
static Registry::RegisterFactory<MetricsServiceSinkFactory, StatsSinkFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
