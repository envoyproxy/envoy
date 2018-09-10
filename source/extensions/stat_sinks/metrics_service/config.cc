#include "extensions/stat_sinks/metrics_service/config.h"

#include "envoy/config/metrics/v2/metrics_service.pb.h"
#include "envoy/config/metrics/v2/metrics_service.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/grpc/async_client_impl.h"
#include "common/network/resolver_impl.h"

#include "extensions/stat_sinks/metrics_service/grpc_metrics_service_impl.h"
#include "extensions/stat_sinks/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace MetricsService {

Stats::SinkPtr MetricsServiceSinkFactory::createStatsSink(const Protobuf::Message& config,
                                                          Server::Instance& server) {
  const auto& sink_config =
      MessageUtil::downcastAndValidate<const envoy::config::metrics::v2::MetricsServiceConfig&>(
          config);
  const auto& grpc_service = sink_config.grpc_service();
  ENVOY_LOG(debug, "Metrics Service gRPC service configuration: {}", grpc_service.DebugString());

  std::shared_ptr<GrpcMetricsStreamer> grpc_metrics_streamer =
      std::make_shared<GrpcMetricsStreamerImpl>(
          server.clusterManager().grpcAsyncClientManager().factoryForGrpcService(
              grpc_service, server.stats(), false),
          server.threadLocal(), server.localInfo());

  return std::make_unique<MetricsServiceSink>(grpc_metrics_streamer, server.timeSystem());
}

ProtobufTypes::MessagePtr MetricsServiceSinkFactory::createEmptyConfigProto() {
  return std::unique_ptr<envoy::config::metrics::v2::MetricsServiceConfig>(
      std::make_unique<envoy::config::metrics::v2::MetricsServiceConfig>());
}

std::string MetricsServiceSinkFactory::name() { return StatsSinkNames::get().MetricsService; }

/**
 * Static registration for the this sink factory. @see RegisterFactory.
 */
static Registry::RegisterFactory<MetricsServiceSinkFactory, Server::Configuration::StatsSinkFactory>
    register_;

} // namespace MetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
