#include "source/extensions/stat_sinks/open_telemetry/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_impl.h"
#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_proto_descriptors.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

Stats::SinkPtr
OpenTelemetrySinkFactory::createStatsSink(const Protobuf::Message& config,
                                          Server::Configuration::ServerFactoryContext& server) {
  validateProtoDescriptors();

  const auto& sink_config = MessageUtil::downcastAndValidate<const SinkConfig&>(
      config, server.messageValidationContext().staticValidationVisitor());

  auto otlp_options = std::make_shared<OtlpOptions>(sink_config);
  std::shared_ptr<OtlpMetricsFlusher> otlp_metrics_flusher =
      std::make_shared<OtlpMetricsFlusherImpl>(otlp_options);

  switch (sink_config.protocol_specifier_case()) {
  case SinkConfig::ProtocolSpecifierCase::kGrpcService: {
    const auto& grpc_service = sink_config.grpc_service();

    auto client_or_error =
        server.clusterManager().grpcAsyncClientManager().getOrCreateRawAsyncClient(
            grpc_service, server.scope(), false);
    THROW_IF_NOT_OK_REF(client_or_error.status());
    std::shared_ptr<OpenTelemetryGrpcMetricsExporter> grpc_metrics_exporter =
        std::make_shared<OpenTelemetryGrpcMetricsExporterImpl>(otlp_options,
                                                               client_or_error.value());

    return std::make_unique<OpenTelemetryGrpcSink>(otlp_metrics_flusher, grpc_metrics_exporter);
  }

  default:
    break;
  }

  throw EnvoyException("unexpected Open Telemetry protocol case num");
}

ProtobufTypes::MessagePtr OpenTelemetrySinkFactory::createEmptyConfigProto() {
  return std::make_unique<SinkConfig>();
}

std::string OpenTelemetrySinkFactory::name() const { return OpenTelemetryName; }

/**
 * Static registration for the this sink factory. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(OpenTelemetrySinkFactory, Server::Configuration::StatsSinkFactory,
                        "envoy.open_telemetry_stat_sink");

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
