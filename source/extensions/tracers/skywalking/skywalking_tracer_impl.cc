#include "source/extensions/tracers/skywalking/skywalking_tracer_impl.h"

#include <memory>

#include "source/common/common/macros.h"
#include "source/common/common/utility.h"
#include "source/common/config/datasource.h"
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

SecretInjector::SecretInjector(
    ThreadLocal::TypedSlot<TlsTracer>& tls,
    Server::Configuration::TransportSocketFactoryContext& ctx,
    const envoy::extensions::transport_sockets::tls::v3::SdsSecretConfig& secret_config)
    : tls_(tls) {
  auto& secret_manager = ctx.secretManager();
  secret_provider_ = secret_manager.findOrCreateGenericSecretProvider(secret_config.sds_config(),
                                                                      secret_config.name(), ctx);

  token_update_handler_ = secret_provider_->addUpdateCallback([&] {
    const auto* new_secret = secret_provider_->secret();
    if (!new_secret) {
      return;
    }
    secret_ = Config::DataSource::read(new_secret->secret(), true, ctx.api());
    tls_.runOnAllThreads([&](OptRef<TlsTracer> tracer) { tracer->value().onTokenUpdate(secret_); });
  });
}

Driver::Driver(const envoy::config::trace::v3::SkyWalkingConfig& proto_config,
               Server::Configuration::TracerFactoryContext& context)
    : tracing_stats_{SKYWALKING_TRACER_STATS(
          POOL_COUNTER_PREFIX(context.serverFactoryContext().scope(), "tracing.skywalking."))},
      tls_(context.serverFactoryContext().threadLocal()) {
  auto& factory_context = context.serverFactoryContext();
  const auto& grpc_service = proto_config.grpc_service();
  const auto& client_config = proto_config.client_config();

  tls_.set([client_config, grpc_service, &factory_context, this](Event::Dispatcher& dispatcher) {
    const auto& backend_token =
        client_config.has_backend_token() ? client_config.backend_token() : "";
    TracerPtr tracer = std::make_unique<Tracer>(std::make_unique<TraceSegmentReporter>(
        factory_context.clusterManager().grpcAsyncClientManager().factoryForGrpcService(
            grpc_service, factory_context.scope(), true),
        dispatcher, factory_context.api().randomGenerator(), tracing_stats_,
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(client_config, max_cache_size,
                                        DEFAULT_DELAYED_SEGMENTS_CACHE_SIZE),
        backend_token));
    return std::make_shared<TlsTracer>(std::move(tracer));
  });

  const auto config = loadConfig(proto_config.client_config(), context);
  tracing_context_factory_ = std::make_unique<TracingContextFactory>(config);

  if (proto_config.client_config().has_backend_token_sds_config()) {
    secret_injector_ =
        std::make_unique<SecretInjector>(tls_, context.transportSocketFactoryContext(),
                                         proto_config.client_config().backend_token_sds_config());
  }
}

Tracing::SpanPtr Driver::startSpan(const Tracing::Config& config,
                                   Tracing::TraceContext& trace_context,
                                   const std::string& operation_name, Envoy::SystemTime start_time,
                                   const Tracing::Decision decision) {
  auto& tracer = tls_.get()->value();
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

TracerConfig Driver::loadConfig(const envoy::config::trace::v3::ClientConfig& client_config,
                                Server::Configuration::TracerFactoryContext& context) {
  TracerConfig config;

  if (!client_config.service_name().empty()) {
    config.set_service_name(client_config.service_name());
  } else if (!context.serverFactoryContext().localInfo().clusterName().empty()) {
    config.set_service_name(context.serverFactoryContext().localInfo().clusterName());
  } else {
    config.set_service_name(DEFAULT_SERVICE_AND_INSTANCE.data());
  }

  if (!client_config.instance_name().empty()) {
    config.set_instance_name(client_config.instance_name());
  } else if (!context.serverFactoryContext().localInfo().nodeName().empty()) {
    config.set_instance_name(context.serverFactoryContext().localInfo().nodeName());
  } else {
    config.set_instance_name(DEFAULT_SERVICE_AND_INSTANCE.data());
  }

  return config;
}

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
