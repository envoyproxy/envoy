#include "source/extensions/tracers/opentelemetry/opentelemetry_tracer_impl.h"

#include <string>

#include "envoy/common/optref.h"
#include "envoy/config/trace/v3/opentelemetry.pb.h"

#include "source/common/common/empty_string.h"
#include "source/common/common/logger.h"
#include "source/common/config/datasource.h"
#include "source/common/config/utility.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/extensions/tracers/opentelemetry/grpc_trace_exporter.h"
#include "source/extensions/tracers/opentelemetry/http_trace_exporter.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_detector.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_provider.h"
#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"
#include "source/extensions/tracers/opentelemetry/span_context.h"
#include "source/extensions/tracers/opentelemetry/span_context_extractor.h"
#include "source/extensions/tracers/opentelemetry/trace_exporter.h"
#include "source/extensions/tracers/opentelemetry/tracer.h"
#include "source/extensions/propagators/opentelemetry/propagator_factory.h"

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

std::vector<std::string>
resolvePropagatorNames(const envoy::config::trace::v3::OpenTelemetryConfig& opentelemetry_config,
                       Api::Api& api) {
  std::vector<std::string> config_propagator_names;
  for (const auto& propagator_name : opentelemetry_config.propagators()) {
    config_propagator_names.push_back(propagator_name);
  }

  // Use temporary propagator to get resolved names, then extract them
  auto temp_propagator =
      Extensions::Propagators::OpenTelemetry::PropagatorFactory::createPropagators(
          config_propagator_names, api);

  // Since we can't easily extract the names from the composite propagator,
  // we'll manually apply the same resolution logic here
  if (!config_propagator_names.empty()) {
    return config_propagator_names;
  }

  // Try to read from OTEL_PROPAGATORS environment variable
  envoy::config::core::v3::DataSource ds;
  ds.set_environment_variable("OTEL_PROPAGATORS");

  std::string env_value = "";
  TRY_NEEDS_AUDIT {
    env_value = THROW_OR_RETURN_VALUE(Config::DataSource::read(ds, true, api), std::string);
  }
  END_TRY catch (const EnvoyException&) {
    // Ignore errors and fall back to default
  }

  if (!env_value.empty()) {
    return Extensions::Propagators::OpenTelemetry::PropagatorFactory::parseOtelPropagatorsEnv(
        env_value);
  }

  // Default
  return {"tracecontext"};
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
    : opentelemetry_config_(opentelemetry_config),
      resolved_propagator_names_(
          resolvePropagatorNames(opentelemetry_config, context.serverFactoryContext().api())),
      tls_slot_ptr_(context.serverFactoryContext().threadLocal().allocateSlot()),
      tracing_stats_{OPENTELEMETRY_TRACER_STATS(
          POOL_COUNTER_PREFIX(context.serverFactoryContext().scope(), "tracing.opentelemetry"))} {
  auto& factory_context = context.serverFactoryContext();

  Resource resource = resource_provider.getResource(
      opentelemetry_config.resource_detectors(), context.serverFactoryContext(),
      opentelemetry_config.service_name().empty() ? kDefaultServiceName
                                                  : opentelemetry_config.service_name());
  ResourceConstSharedPtr resource_ptr = std::make_shared<Resource>(std::move(resource));

  if (opentelemetry_config.has_grpc_service() && opentelemetry_config.has_http_service()) {
    throw EnvoyException(
        "OpenTelemetry Tracer cannot have both gRPC and HTTP exporters configured. "
        "OpenTelemetry tracer will be disabled.");
  }

  // Create the sampler if configured
  SamplerSharedPtr sampler = tryCreateSamper(opentelemetry_config, context);

  // Create propagators based on configuration and environment variables
  std::vector<std::string> config_propagator_names;
  for (const auto& propagator_name : opentelemetry_config.propagators()) {
    config_propagator_names.push_back(propagator_name);
  }

  // Use new factory method that supports OTEL_PROPAGATORS environment variable
  Propagators::OpenTelemetry::CompositePropagatorPtr propagator =
      Extensions::Propagators::OpenTelemetry::PropagatorFactory::createPropagators(
          config_propagator_names, factory_context.api());

  // Create the tracer in Thread Local Storage.
  tls_slot_ptr_->set([opentelemetry_config, &factory_context, this, resource_ptr, sampler,
                      propagator = std::make_shared<CompositePropagatorPtr>(std::move(propagator))](
                         Event::Dispatcher& dispatcher) mutable {
    OpenTelemetryTraceExporterPtr exporter;
    if (opentelemetry_config.has_grpc_service()) {
      auto factory_or_error =
          factory_context.clusterManager().grpcAsyncClientManager().factoryForGrpcService(
              opentelemetry_config.grpc_service(), factory_context.scope(), true);
      THROW_IF_NOT_OK_REF(factory_or_error.status());
      Grpc::AsyncClientFactoryPtr&& factory = std::move(factory_or_error.value());
      const Grpc::RawAsyncClientSharedPtr& async_client_shared_ptr =
          THROW_OR_RETURN_VALUE(factory->createUncachedRawAsyncClient(), Grpc::RawAsyncClientPtr);
      exporter = std::make_unique<OpenTelemetryGrpcTraceExporter>(async_client_shared_ptr);
    } else if (opentelemetry_config.has_http_service()) {
      exporter = std::make_unique<OpenTelemetryHttpTraceExporter>(
          factory_context.clusterManager(), opentelemetry_config.http_service());
    }
    // Get the max cache size from config
    uint64_t max_cache_size = PROTOBUF_GET_WRAPPED_OR_DEFAULT(opentelemetry_config, max_cache_size,
                                                              DEFAULT_MAX_CACHE_SIZE);
    TracerPtr tracer = std::make_unique<Tracer>(
        std::move(exporter), factory_context.timeSource(), factory_context.api().randomGenerator(),
        factory_context.runtime(), dispatcher, tracing_stats_, resource_ptr, sampler,
        max_cache_size, std::move(*propagator));
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

  // Create a copy of the propagator for the span context extractor
  // Note: We need to create a new propagator instance since SpanContextExtractor expects ownership
  // Use the resolved propagator names which include environment variable resolution
  auto extractor_propagator =
      Extensions::Propagators::OpenTelemetry::PropagatorFactory::createPropagators(
          resolved_propagator_names_);
  SpanContextExtractor extractor(trace_context, std::move(extractor_propagator));

  const auto span_kind = getSpanKind(config);
  if (!extractor.propagationHeaderPresent()) {
    // No propagation header, so we can create a fresh span with the given decision.
    Tracing::SpanPtr new_open_telemetry_span =
        tracer.startSpan(operation_name, stream_info, stream_info.startTime(), tracing_decision,
                         trace_context, span_kind);
    return new_open_telemetry_span;
  } else {
    // Try to extract the span context. If we can't, just return a null span.
    absl::StatusOr<SpanContext> span_context = extractor.extractSpanContext();
    if (span_context.ok()) {
      return tracer.startSpan(operation_name, stream_info, stream_info.startTime(),
                              span_context.value(), trace_context, span_kind);
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
