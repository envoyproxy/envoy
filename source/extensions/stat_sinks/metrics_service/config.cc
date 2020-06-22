#include "extensions/stat_sinks/metrics_service/config.h"

#include "envoy/config/metrics/v3/metrics_service.pb.h"
#include "envoy/config/metrics/v3/metrics_service.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/common/assert.h"
#include "common/grpc/async_client_impl.h"
#include "common/network/resolver_impl.h"

#include "extensions/stat_sinks/metrics_service/grpc_metrics_proto_descriptors.h"
#include "extensions/stat_sinks/metrics_service/grpc_metrics_service_impl.h"
#include "extensions/stat_sinks/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace MetricsService {

Stats::SinkPtr MetricsServiceSinkFactory::createStatsSink(const Protobuf::Message& config,
                                                          Server::Instance& server) {
  validateProtoDescriptors();

  const auto& sink_config =
      MessageUtil::downcastAndValidate<const envoy::config::metrics::v3::MetricsServiceConfig&>(
          config, server.messageValidationContext().staticValidationVisitor());
  const auto& grpc_service = sink_config.grpc_service();
  const auto& transport_api_version = sink_config.transport_api_version();
  ENVOY_LOG(debug, "Metrics Service gRPC service configuration: {}", grpc_service.DebugString());

  std::shared_ptr<GrpcMetricsStreamer> grpc_metrics_streamer =
      std::make_shared<GrpcMetricsStreamerImpl>(
          server.clusterManager().grpcAsyncClientManager().factoryForGrpcService(
              grpc_service, server.stats(), false),
          server.localInfo(), transport_api_version);

  return std::make_unique<MetricsServiceSink>(
      grpc_metrics_streamer, server.timeSource(),
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(sink_config, report_counters_as_deltas, false));
}

ProtobufTypes::MessagePtr MetricsServiceSinkFactory::createEmptyConfigProto() {
  return std::unique_ptr<envoy::config::metrics::v3::MetricsServiceConfig>(
      std::make_unique<envoy::config::metrics::v3::MetricsServiceConfig>());
}

std::string MetricsServiceSinkFactory::name() const { return StatsSinkNames::get().MetricsService; }

/**
 * Static registration for the this sink factory. @see RegisterFactory.
 */
REGISTER_FACTORY(MetricsServiceSinkFactory,
                 Server::Configuration::StatsSinkFactory){"envoy.metrics_service"};

} // namespace MetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
