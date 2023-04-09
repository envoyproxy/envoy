#include "source/extensions/stat_sinks/open_telemetry/config.h"

#include "envoy/extensions/stat_sinks/open_telemetry/v3/open_telemetry.pb.h"
#include "envoy/extensions/stat_sinks/open_telemetry/v3/open_telemetry.pb.validate.h"

#include "envoy/registry/registry.h"

#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_proto_descriptors.h"
#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

using SinkConfig = envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig;

Stats::SinkPtr
OpenTelemetrySinkFactory::createStatsSink(const Protobuf::Message& config,
                                          Server::Configuration::ServerFactoryContext& server) {
  validateProtoDescriptors();

  const auto& sink_config =
      MessageUtil::downcastAndValidate<const SinkConfig&>(config,
          server.messageValidationContext().staticValidationVisitor());

  switch (sink_config.protocol_specifier_case()) {
    case SinkConfig::ProtocolSpecifierCase::kGrpcService: {
      const auto& grpc_service = sink_config.grpc_service();

      std::shared_ptr<OpenTelemetryGrpcMetricsExporter> otlp_metrics_exporter =
          std::make_shared<OpenTelemetryGrpcMetricsExporterImpl>(
          server.clusterManager().grpcAsyncClientManager().getOrCreateRawAsyncClient(
              grpc_service, server.scope(), false));

      return std::make_unique<OpenTelemetryGrpcSink>(otlp_metrics_exporter,
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(sink_config, report_counters_as_deltas, false),
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(sink_config, report_histograms_as_deltas, false),
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(sink_config, emit_tags_as_labels, true));
    }

    case SinkConfig::ProtocolSpecifierCase::PROTOCOL_SPECIFIER_NOT_SET:
      break;
  }

  throw EnvoyException("unexpected Open Telemetry protocol case num");
}

ProtobufTypes::MessagePtr OpenTelemetrySinkFactory::createEmptyConfigProto() {
  return std::make_unique<SinkConfig>();
}

std::string OpenTelemetrySinkFactory::name() const { return "envoy.stat_sinks.open_telemetry"; }

/**
 * Static registration for the this sink factory. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(OpenTelemetrySinkFactory, Server::Configuration::StatsSinkFactory,
                        "envoy.open_telemetry_stat_sink");

} // namespace OpenTelemetryGrpc
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
