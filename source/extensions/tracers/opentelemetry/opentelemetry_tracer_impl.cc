#include "source/extensions/tracers/opentelemetry/opentelemetry_tracer_impl.h"

#include <string>

#include "api/envoy/config/trace/v3/opentelemetry.pb.h"
#include "envoy/common/exception.h"
#include "envoy/common/optref.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/logger.h"
#include "source/common/config/utility.h"
#include "source/common/http/http_service_headers.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/common/tracing/tracer_config_impl.h"
#include "source/extensions/tracers/opentelemetry/grpc_trace_exporter.h"
#include "source/extensions/tracers/opentelemetry/http_trace_exporter.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_detector.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_provider.h"
#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"
#include "source/extensions/tracers/opentelemetry/span_context.h"
#include "source/extensions/tracers/opentelemetry/span_context_extractor.h"
#include "source/extensions/tracers/opentelemetry/trace_exporter.h"
#include "source/extensions/tracers/opentelemetry/tracer.h"
#include "fmt/format.h"
#include "opentelemetry/proto/collector/trace/v1/trace_service.pb.h"
#include "opentelemetry/proto/trace/v1/trace.pb.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

namespace {

// Default max cache size for OpenTelemetry tracer
static constexpr uint64_t DEFAULT_MAX_CACHE_SIZE = 1024;

SamplerSharedPtr
tryCreateSamper(const envoy::config::trace::v3::OpenTelemetryConfig& opentelemetry_config,
                Server::Configuration::TracerFactoryContext& context) {
  SamplerSharedPtr sampler;
  if (opentelemetry_config.has_sampler()) {
    auto& sampler_config = opentelemetry_config.sampler();
    auto* factory = Envoy::Config::Utility::getFactory<SamplerFactory>(sampler_config);
    if (!factory) {
      throw EnvoyException(fmt::format("Sampler factory not found: '{}'", sampler_config.name()));
    }
    sampler = factory->createSampler(sampler_config.typed_config(), context);
  }
  return sampler;
}

OTelSpanKind getSpanKind(const Tracing::Config& config) {
  // If this is downstream span that be created by 'startSpan' for downstream request, then
  // set the span type based on the spawnUpstreamSpan flag and traffic direction:
  // * If separate tracing span will be created for upstream request, then set span type to
  //   SERVER because the downstream span should be server span in trace chain.
  // * If separate tracing span will not be created for upstream request, that means the
  //   Envoy will not be treated as independent hop in trace chain and then set span type
  //   based on the traffic direction.
  return (config.spawnUpstreamSpan() ? ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER
          : config.operationName() == Tracing::OperationName::Egress
              ? ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_CLIENT
              : ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER);
}

} // namespace

Driver::Driver(const envoy::config::trace::v3::OpenTelemetryConfig& opentelemetry_config,
               Server::Configuration::TracerFactoryContext& context)
    : Driver(opentelemetry_config, context, ResourceProviderImpl{}) {}

Driver::Driver(const envoy::config::trace::v3::OpenTelemetryConfig& opentelemetry_config,
               Server::Configuration::TracerFactoryContext& context,
               const ResourceProvider& resource_provider)
    : tls_slot_ptr_(context.serverFactoryContext().threadLocal().allocateSlot()),
      tracing_stats_{OPENTELEMETRY_TRACER_STATS(
          POOL_COUNTER_PREFIX(context.serverFactoryContext().scope(), "tracing.opentelemetry"))} {
  auto& factory_context = context.serverFactoryContext();

  ResourceProviderOptions options;
  options.set_telemetry_sdk_resource_attributes = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      opentelemetry_config, set_telemetry_sdk_resource_attributes, true);
  options.set_service_name_resource_attribute = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      opentelemetry_config, set_service_name_resource_attribute, true);

  Resource resource = resource_provider.getResource(
      opentelemetry_config.resource_detectors(), context.serverFactoryContext(),
      opentelemetry_config.service_name().empty() ? kDefaultServiceName
                                                  : opentelemetry_config.service_name(),
      options);
  ResourceConstSharedPtr resource_ptr = std::make_shared<Resource>(std::move(resource));

  const int exporter_count = (opentelemetry_config.has_grpc_service() ? 1 : 0) +
                             (opentelemetry_config.has_http_service() ? 1 : 0) +
                             (opentelemetry_config.has_exporter() ? 1 : 0);
  if (exporter_count != 1) {
    throw EnvoyException(
        "OpenTelemetry Tracer must have exactly one of gRPC, HTTP, or custom "
        "exporter configured.");
  }

  std::shared_ptr<Grpc::AsyncClientFactory> grpc_client_factory;
  if (opentelemetry_config.has_grpc_service()) {
    auto factory_or_error =
        factory_context.clusterManager()
            .grpcAsyncClientManager()
            .factoryForGrpcService(opentelemetry_config.grpc_service(),
                                   factory_context.scope(), true);
    THROW_IF_NOT_OK_REF(factory_or_error.status());
    grpc_client_factory = std::move(factory_or_error.value());
  }

  OpenTelemetryTraceExporterFactory* custom_exporter_factory = nullptr;
  std::shared_ptr<const Protobuf::Message> shared_unpacked_config;
  std::shared_ptr<Tracing::TracerFactoryContextImpl> worker_factory_context;
  if (opentelemetry_config.has_exporter()) {
    auto& exporter_config = opentelemetry_config.exporter();
    custom_exporter_factory =
        Envoy::Config::Utility::getFactory<OpenTelemetryTraceExporterFactory>(
            exporter_config);
    if (!custom_exporter_factory) {
      throw EnvoyException(
          fmt::format("OpenTelemetry trace exporter factory not found: '{}'",
                      exporter_config.name()));
    }
    ProtobufTypes::MessagePtr unpacked_config =
        custom_exporter_factory->createEmptyConfigProto();
    if (unpacked_config == nullptr) {
      throw EnvoyException(
          fmt::format("OpenTelemetry trace exporter factory '{}' returned "
                      "nullptr from createEmptyConfigProto()",
                      exporter_config.name()));
    }
    const absl::Status status = Envoy::Config::Utility::translateOpaqueConfig(
        exporter_config.typed_config(), context.messageValidationVisitor(),
        *unpacked_config);
    if (!status.ok()) {
      throw EnvoyException(
          fmt::format("Failed to translate opaque config for OpenTelemetry "
                      "trace exporter factory '{}': {}",
                      exporter_config.name(), status.message()));
    }
    shared_unpacked_config = std::move(unpacked_config);
    // Main-thread validation occurred during translateOpaqueConfig above.
    // Use NullValidationVisitor for worker-thread context to ensure
    // thread-safety.
    worker_factory_context =
        std::make_shared<Tracing::TracerFactoryContextImpl>(
            factory_context, ProtobufMessage::getNullValidationVisitor());
  }

  // Create the sampler if configured
  SamplerSharedPtr sampler = tryCreateSamper(opentelemetry_config, context);

  const uint64_t max_cache_size = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      opentelemetry_config, max_cache_size, DEFAULT_MAX_CACHE_SIZE);
  const bool set_instrumentation_scope = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      opentelemetry_config, set_instrumentation_scope, true);

  std::shared_ptr<const envoy::config::core::v3::HttpService> http_service;
  std::shared_ptr<const Http::HttpServiceHeadersApplicator> headers_applicator;
  if (opentelemetry_config.has_http_service()) {
    http_service = std::make_shared<const envoy::config::core::v3::HttpService>(
        opentelemetry_config.http_service());
    headers_applicator = Http::HttpServiceHeadersApplicator::createOrThrow(
        *http_service, factory_context);
  }

  // Create the tracer in Thread Local Storage.
  tls_slot_ptr_->set([http_service, max_cache_size, set_instrumentation_scope,
                      &factory_context, this, resource_ptr, sampler,
                      headers_applicator, grpc_client_factory,
                      custom_exporter_factory, shared_unpacked_config,
                      worker_factory_context](Event::Dispatcher& dispatcher) {
    OpenTelemetryTraceExporterPtr exporter;
    if (grpc_client_factory != nullptr) {
      auto async_client_or_error =
          grpc_client_factory->createUncachedRawAsyncClient();
      if (async_client_or_error.ok()) {
        Grpc::RawAsyncClientSharedPtr async_client_shared_ptr =
            std::move(async_client_or_error.value());
        exporter = std::make_unique<OpenTelemetryGrpcTraceExporter>(
            async_client_shared_ptr);
      } else {
        ENVOY_LOG(
            error,
            "Failed to create gRPC async client for OpenTelemetry tracer: {}",
            async_client_or_error.status());
      }
    } else if (http_service != nullptr) {
      ASSERT(headers_applicator != nullptr);
      exporter = std::make_unique<OpenTelemetryHttpTraceExporter>(
          factory_context.clusterManager(), *http_service, headers_applicator);
    } else if (custom_exporter_factory != nullptr &&
               shared_unpacked_config != nullptr &&
               worker_factory_context != nullptr) {
      try {
        exporter = custom_exporter_factory->createExporter(
            *shared_unpacked_config, *worker_factory_context);
      } catch (const std::exception& e) {
        ENVOY_LOG(error,
                  "Failed to create custom OpenTelemetry trace exporter: {}",
                  e.what());
      }
    }
    if (exporter == nullptr) {
      ENVOY_LOG(warn,
                "OpenTelemetry tracer initialized without a valid exporter; "
                "spans will be dropped.");
    }
    TracerPtr tracer = std::make_unique<Tracer>(
        std::move(exporter), factory_context.timeSource(), factory_context.api().randomGenerator(),
        factory_context.runtime(), dispatcher, tracing_stats_, resource_ptr, sampler,
        max_cache_size, set_instrumentation_scope);
    return std::make_shared<TlsTracer>(std::move(tracer));
  });
}

Tracing::SpanPtr Driver::startSpan(const Tracing::Config& config,
                                   Tracing::TraceContext& trace_context,
                                   const StreamInfo::StreamInfo& stream_info,
                                   const std::string& operation_name,
                                   Tracing::Decision tracing_decision) {
  // Get tracer from TLS and start span.
  auto& tracer = tls_slot_ptr_->getTyped<Driver::TlsTracer>().tracer();
  SpanContextExtractor extractor(trace_context);
  const auto span_kind = getSpanKind(config);
  auto applyTracingDecision = [&tracing_decision](Tracing::SpanPtr span) -> Tracing::SpanPtr {
    if (!tracing_decision.traced) {
      span->setSampled(false);
    }
    return span;
  };
  if (!extractor.propagationHeaderPresent()) {
    // No propagation header, so we can create a fresh span with the given decision.
    return applyTracingDecision(tracer.startSpan(operation_name, stream_info,
                                                 stream_info.startTime(), tracing_decision,
                                                 trace_context, span_kind));
  } else {
    // Try to extract the span context. If we can't, just return a null span.
    absl::StatusOr<SpanContext> span_context = extractor.extractSpanContext();
    if (span_context.ok()) {
      return applyTracingDecision(tracer.startSpan(operation_name, stream_info,
                                                   stream_info.startTime(), span_context.value(),
                                                   trace_context, span_kind));
    } else {
      ENVOY_LOG(trace, "Unable to extract span context: ", span_context.status());
      return std::make_unique<Tracing::NullSpan>();
    }
  }
}

Driver::TlsTracer::TlsTracer(TracerPtr tracer) : tracer_(std::move(tracer)) {}

Tracer& Driver::TlsTracer::tracer() {
  ASSERT(tracer_);
  return *tracer_;
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
