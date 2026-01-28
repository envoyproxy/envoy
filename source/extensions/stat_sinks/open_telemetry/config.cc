#include "source/extensions/stat_sinks/open_telemetry/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_http_impl.h"
#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_impl.h"
#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_proto_descriptors.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_provider.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

absl::StatusOr<Stats::SinkPtr>
OpenTelemetrySinkFactory::createStatsSink(const Protobuf::Message& config,
                                          Server::Configuration::ServerFactoryContext& server) {
  validateProtoDescriptors();

  const auto& sink_config = MessageUtil::downcastAndValidate<const SinkConfig&>(
      config, server.messageValidationContext().staticValidationVisitor());

  Tracers::OpenTelemetry::ResourceProviderPtr resource_provider =
      std::make_unique<Tracers::OpenTelemetry::ResourceProviderImpl>();
  auto otlp_options = std::make_shared<OtlpOptions>(
      sink_config,
      resource_provider->getResource(sink_config.resource_detectors(), server,
                                     /*service_name=*/""),
      server);
  std::shared_ptr<OtlpMetricsFlusher> otlp_metrics_flusher =
      std::make_shared<OtlpMetricsFlusherImpl>(otlp_options);

  switch (sink_config.protocol_specifier_case()) {
  case SinkConfig::ProtocolSpecifierCase::kGrpcService: {
    const auto& grpc_service = sink_config.grpc_service();

    auto client_or_error =
        server.clusterManager().grpcAsyncClientManager().getOrCreateRawAsyncClient(
            grpc_service, server.scope(), false);
    RETURN_IF_NOT_OK_REF(client_or_error.status());
    std::shared_ptr<OtlpMetricsExporter> grpc_metrics_exporter =
        std::make_shared<OpenTelemetryGrpcMetricsExporterImpl>(otlp_options,
                                                               client_or_error.value());

    return std::make_unique<OpenTelemetrySink>(
        otlp_metrics_flusher, grpc_metrics_exporter,
        server.timeSource().systemTime().time_since_epoch().count());
  }

  case SinkConfig::ProtocolSpecifierCase::kHttpService: {
    std::shared_ptr<OtlpMetricsExporter> http_metrics_exporter =
        std::make_shared<OpenTelemetryHttpMetricsExporter>(server.clusterManager(),
                                                           sink_config.http_service());

    return std::make_unique<OpenTelemetrySink>(
        otlp_metrics_flusher, http_metrics_exporter,
        server.timeSource().systemTime().time_since_epoch().count());
  }

  default:
    break;
  }

  return absl::InvalidArgumentError("unexpected Open Telemetry protocol case num");
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
