#include "source/extensions/tracers/skywalking/skywalking_tracer_impl.h"

#include <memory>

#include "source/common/common/macros.h"
#include "source/common/common/utility.h"
#include "source/common/http/path_utility.h"

#include "cpp2sky/propagation.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

namespace {
constexpr uint32_t DEFAULT_DELAYED_SEGMENTS_CACHE_SIZE = 1024;

// When the user does not provide any available configuration, in order to ensure that the service
// name and instance name are not empty, use this value as the default identifier. In practice,
// user should provide accurate configuration as much as possible to avoid using the default value.
constexpr absl::string_view DEFAULT_SERVICE_AND_INSTANCE = "EnvoyProxy";
} // namespace

using cpp2sky::createSpanContext;
using cpp2sky::SpanContextPtr;
using cpp2sky::TracerException;

Driver::Driver(const envoy::config::trace::v3::SkyWalkingConfig& proto_config,
               Server::Configuration::TracerFactoryContext& context)
    : tracing_stats_{SKYWALKING_TRACER_STATS(
          POOL_COUNTER_PREFIX(context.serverFactoryContext().scope(), "tracing.skywalking."))},
      tls_slot_ptr_(context.serverFactoryContext().threadLocal().allocateSlot()) {
  loadConfig(proto_config.client_config(), context.serverFactoryContext());
  tracing_context_factory_ = std::make_unique<TracingContextFactory>(config_);
  auto& factory_context = context.serverFactoryContext();
  tls_slot_ptr_->set([proto_config, &factory_context, this](Event::Dispatcher& dispatcher) {
    TracerPtr tracer = std::make_unique<Tracer>(std::make_unique<TraceSegmentReporter>(
        factory_context.clusterManager().grpcAsyncClientManager().factoryForGrpcService(
            proto_config.grpc_service(), factory_context.scope(), true),
        dispatcher, factory_context.api().randomGenerator(), tracing_stats_,
        config_.delayed_buffer_size(), config_.token()));
    return std::make_shared<TlsTracer>(std::move(tracer));
  });
}

Tracing::SpanPtr Driver::startSpan(const Tracing::Config& config,
                                   Tracing::TraceContext& trace_context,
                                   const std::string& operation_name, Envoy::SystemTime start_time,
                                   const Tracing::Decision decision) {
  auto& tracer = tls_slot_ptr_->getTyped<Driver::TlsTracer>().tracer();
  TracingContextPtr tracing_context;
  // TODO(shikugawa): support extension span header.
  auto propagation_header = trace_context.getByKey(skywalkingPropagationHeaderKey());
  if (!propagation_header.has_value()) {
    tracing_context = tracing_context_factory_->create();
    // Sampling status is always true on SkyWalking. But with disabling skip_analysis,
    // this span can't be analyzed.
    if (!decision.traced) {
      tracing_context->setSkipAnalysis();
    }
  } else {
    auto header_value_string = propagation_header.value();
    try {
      SpanContextPtr span_context =
          createSpanContext(toStdStringView(header_value_string)); // NOLINT(std::string_view)
      tracing_context = tracing_context_factory_->create(span_context);
    } catch (TracerException& e) {
      ENVOY_LOG(warn, "New SkyWalking Span/Segment cannot be created for error: {}", e.what());
      return std::make_unique<Tracing::NullSpan>();
    }
  }

  return tracer.startSpan(config, start_time, operation_name, tracing_context, nullptr);
}

void Driver::loadConfig(const envoy::config::trace::v3::ClientConfig& client_config,
                        Server::Configuration::ServerFactoryContext& server_factory_context) {
  config_.set_service_name(!client_config.service_name().empty()
                               ? client_config.service_name()
                               : (!server_factory_context.localInfo().clusterName().empty()
                                      ? server_factory_context.localInfo().clusterName()
                                      : DEFAULT_SERVICE_AND_INSTANCE.data()));
  config_.set_instance_name(!client_config.instance_name().empty()
                                ? client_config.service_name()
                                : (!server_factory_context.localInfo().nodeName().empty()
                                       ? server_factory_context.localInfo().nodeName()
                                       : DEFAULT_SERVICE_AND_INSTANCE.data()));
  config_.set_token(client_config.backend_token());
  config_.set_delayed_buffer_size(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      client_config, max_cache_size, DEFAULT_DELAYED_SEGMENTS_CACHE_SIZE));
}

Driver::TlsTracer::TlsTracer(TracerPtr tracer) : tracer_(std::move(tracer)) {}

Tracer& Driver::TlsTracer::tracer() {
  ASSERT(tracer_);
  return *tracer_;
}

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
