#include "source/extensions/stat_sinks/metrics_service/config.h"

#include "envoy/config/metrics/v3/metrics_service.pb.h"
#include "envoy/config/metrics/v3/metrics_service.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/common/assert.h"
#include "source/common/config/utility.h"
#include "source/common/grpc/async_client_impl.h"
#include "source/extensions/stat_sinks/metrics_service/grpc_metrics_proto_descriptors.h"
#include "source/extensions/stat_sinks/metrics_service/grpc_metrics_service_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace MetricsService {

absl::StatusOr<Stats::SinkPtr>
MetricsServiceSinkFactory::createStatsSink(const Protobuf::Message& config,
                                           Server::Configuration::ServerFactoryContext& server) {
  validateProtoDescriptors();

  const auto& sink_config =
      MessageUtil::downcastAndValidate<const envoy::config::metrics::v3::MetricsServiceConfig&>(
          config, server.messageValidationContext().staticValidationVisitor());
  const auto& grpc_service = sink_config.grpc_service();
  RETURN_IF_NOT_OK(Config::Utility::checkTransportVersion(sink_config));
  ENVOY_LOG(debug, "Metrics Service gRPC service configuration: {}", grpc_service.DebugString());

  auto client_or_error = server.clusterManager().grpcAsyncClientManager().getOrCreateRawAsyncClient(
      grpc_service, server.scope(), false);
  RETURN_IF_NOT_OK_REF(client_or_error.status());
  std::shared_ptr<GrpcMetricsStreamer<envoy::service::metrics::v3::StreamMetricsMessage,
                                      envoy::service::metrics::v3::StreamMetricsResponse>>
      grpc_metrics_streamer = std::make_shared<GrpcMetricsStreamerImpl>(
          client_or_error.value(), server.localInfo(), sink_config.batch_size());

  return std::make_unique<MetricsServiceSink<envoy::service::metrics::v3::StreamMetricsMessage,
                                             envoy::service::metrics::v3::StreamMetricsResponse>>(
      grpc_metrics_streamer,
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(sink_config, report_counters_as_deltas, false),
      sink_config.emit_tags_as_labels(), sink_config.histogram_emit_mode());
}

ProtobufTypes::MessagePtr MetricsServiceSinkFactory::createEmptyConfigProto() {
  return std::unique_ptr<envoy::config::metrics::v3::MetricsServiceConfig>(
      std::make_unique<envoy::config::metrics::v3::MetricsServiceConfig>());
}

std::string MetricsServiceSinkFactory::name() const { return MetricsServiceName; }

/**
 * Static registration for the this sink factory. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(MetricsServiceSinkFactory, Server::Configuration::StatsSinkFactory,
                        "envoy.metrics_service");

} // namespace MetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
