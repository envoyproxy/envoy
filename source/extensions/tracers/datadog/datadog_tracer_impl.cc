#include "extensions/tracers/datadog/datadog_tracer_impl.h"

#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

Driver::TlsTracer::TlsTracer(const std::shared_ptr<opentracing::Tracer>& tracer,
                             TraceReporterPtr&& reporter, Driver& driver)
    : tracer_(tracer), reporter_(std::move(reporter)), driver_(driver) {}

Driver::Driver(const envoy::config::trace::v2::DatadogConfig& datadog_config,
               Upstream::ClusterManager& cluster_manager, Stats::Store& stats,
               ThreadLocal::SlotAllocator& tls, Runtime::Loader& runtime)
    : OpenTracingDriver{stats},
      cm_(cluster_manager), tracer_stats_{DATADOG_TRACER_STATS(
                                POOL_COUNTER_PREFIX(stats, "tracing.datadog."))},
      tls_(tls.allocateSlot()), runtime_(runtime) {

  Upstream::ThreadLocalCluster* cluster = cm_.get(datadog_config.collector_cluster());
  if (!cluster) {
    throw EnvoyException(fmt::format("{} collector cluster is not defined on cluster manager level",
                                     datadog_config.collector_cluster()));
  }
  cluster_ = cluster->info();

  // Default tracer options.
  tracer_options_.operation_name_override = "envoy.proxy";
  tracer_options_.service = "envoy";
  tracer_options_.priority_sampling = true;

  // Configuration overrides for tracer options.
  if (!datadog_config.service_name().empty()) {
    tracer_options_.service = datadog_config.service_name();
  }
  if (!datadog_config.priority_sampling()) {
    tracer_options_.priority_sampling = false;
  }

  tls_->set([this](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    auto tp = datadog::opentracing::makeTracerAndEncoder(tracer_options_);
    auto tracer = std::get<0>(tp);
    auto encoder = std::get<1>(tp);
    TraceReporterPtr reporter(new TraceReporter(encoder, *this, dispatcher));
    return ThreadLocal::ThreadLocalObjectSharedPtr{
        new TlsTracer(tracer, std::move(reporter), *this)};
  });
}

opentracing::Tracer& Driver::tracer() { return *tls_->getTyped<TlsTracer>().tracer_; }

TraceReporter::TraceReporter(TraceEncoderPtr encoder, Driver& driver, Event::Dispatcher& dispatcher)
    : driver_(driver), encoder_(encoder) {
  flush_timer_ = dispatcher.createTimer([this]() -> void {
    driver_.tracerStats().timer_flushed_.inc();
    flushTraces();
    enableTimer();
  });

  enableTimer();
}

void TraceReporter::enableTimer() {
  const uint64_t flush_interval =
      driver_.runtime().snapshot().getInteger("tracing.datadog.flush_interval_ms", 1000U);
  flush_timer_->enableTimer(std::chrono::milliseconds(flush_interval));
}

void TraceReporter::flushTraces() {
  auto pendingTraces = encoder_->pendingTraces();
  ENVOY_LOG(debug, "flushing traces: {} traces", pendingTraces);
  if (pendingTraces) {
    driver_.tracerStats().traces_sent_.add(pendingTraces);

    Http::MessagePtr message(new Http::RequestMessageImpl());
    message->headers().insertMethod().value().setReference(Http::Headers::get().MethodValues.Post);
    message->headers().insertPath().value(encoder_->path());
    message->headers().insertHost().value(driver_.cluster()->name());
    for (auto& h : encoder_->headers()) {
      message->headers().addCopy(Http::LowerCaseString(h.first), h.second);
    }

    Buffer::InstancePtr body(new Buffer::OwnedImpl());
    body->add(encoder_->payload());
    message->body() = std::move(body);
    ENVOY_LOG(debug, "submitting {} trace(s) to {} with payload {}", pendingTraces,
              encoder_->path(), encoder_->payload().size());

    const uint64_t timeout =
        driver_.runtime().snapshot().getInteger("tracing.datadog.request_timeout", 1000U);
    driver_.clusterManager()
        .httpAsyncClientForCluster(driver_.cluster()->name())
        .send(std::move(message), *this, std::chrono::milliseconds(timeout));

    encoder_->clearTraces();
  }
}

void TraceReporter::onFailure(Http::AsyncClient::FailureReason) {
  ENVOY_LOG(debug, "failure submitting traces to datadog agent");
  driver_.tracerStats().reports_failed_.inc();
}

void TraceReporter::onSuccess(Http::MessagePtr&& http_response) {
  uint64_t responseStatus = Http::Utility::getResponseStatus(http_response->headers());
  if (responseStatus != enumToInt(Http::Code::OK)) {
    ENVOY_LOG(debug, "unexpected HTTP response code from datadog agent: {}", responseStatus);
    driver_.tracerStats().reports_dropped_.inc();
  } else {
    ENVOY_LOG(debug, "traces successfully submitted to datadog agent");
    driver_.tracerStats().reports_sent_.inc();
    if (driver_.tracerOptions().priority_sampling) {
      encoder_->handleResponse(http_response->body()->toString());
    }
  }
}

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
