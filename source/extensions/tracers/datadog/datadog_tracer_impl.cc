#include "extensions/tracers/datadog/datadog_tracer_impl.h"

#include "envoy/config/trace/v3/datadog.pb.h"

#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/common/version.h"
#include "common/config/utility.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

Driver::TlsTracer::TlsTracer(const std::shared_ptr<opentracing::Tracer>& tracer,
                             TraceReporterPtr&& reporter, Driver& driver)
    : tracer_(tracer), reporter_(std::move(reporter)), driver_(driver) {}

Driver::Driver(const envoy::config::trace::v3::DatadogConfig& datadog_config,
               Upstream::ClusterManager& cluster_manager, Stats::Scope& scope,
               ThreadLocal::SlotAllocator& tls, Runtime::Loader&)
    : OpenTracingDriver{scope},
      cm_(cluster_manager), tracer_stats_{DATADOG_TRACER_STATS(
                                POOL_COUNTER_PREFIX(scope, "tracing.datadog."))},
      tls_(tls.allocateSlot()) {

  Config::Utility::checkCluster(TracerNames::get().Datadog, datadog_config.collector_cluster(), cm_,
                                /* allow_added_via_api */ true);
  cluster_ = datadog_config.collector_cluster();

  // Default tracer options.
  tracer_options_.version = absl::StrCat("envoy ", Envoy::VersionInfo::version());
  tracer_options_.operation_name_override = "envoy.proxy";
  tracer_options_.service = "envoy";
  tracer_options_.inject = std::set<datadog::opentracing::PropagationStyle>{
      datadog::opentracing::PropagationStyle::Datadog};
  tracer_options_.extract = std::set<datadog::opentracing::PropagationStyle>{
      datadog::opentracing::PropagationStyle::Datadog};

  // Configuration overrides for tracer options.
  if (!datadog_config.service_name().empty()) {
    tracer_options_.service = datadog_config.service_name();
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

TraceReporter::TraceReporter(TraceEncoderSharedPtr encoder, Driver& driver,
                             Event::Dispatcher& dispatcher)
    : driver_(driver), encoder_(encoder),
      collector_cluster_(driver_.clusterManager(), driver_.cluster()) {
  flush_timer_ = dispatcher.createTimer([this]() -> void {
    for (auto& h : encoder_->headers()) {
      lower_case_headers_.emplace(h.first, Http::LowerCaseString{h.first});
    }
    driver_.tracerStats().timer_flushed_.inc();
    flushTraces();
    enableTimer();
  });

  enableTimer();
}

void TraceReporter::enableTimer() {
  // The duration for this timer should not be a factor of the
  // datadog-agent's read timer of 5000ms.
  // Further details in https://github.com/envoyproxy/envoy/pull/5358
  flush_timer_->enableTimer(std::chrono::milliseconds(900U));
}

void TraceReporter::flushTraces() {
  auto pendingTraces = encoder_->pendingTraces();
  if (pendingTraces) {
    ENVOY_LOG(debug, "flushing traces: {} traces", pendingTraces);
    driver_.tracerStats().traces_sent_.add(pendingTraces);

    Http::RequestMessagePtr message(new Http::RequestMessageImpl());
    message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Post);
    message->headers().setReferencePath(encoder_->path());
    message->headers().setReferenceHost(driver_.cluster());
    for (auto& h : encoder_->headers()) {
      message->headers().setReferenceKey(lower_case_headers_.at(h.first), h.second);
    }

    Buffer::InstancePtr body(new Buffer::OwnedImpl());
    body->add(encoder_->payload());
    message->body() = std::move(body);
    ENVOY_LOG(debug, "submitting {} trace(s) to {} with payload size {}", pendingTraces,
              encoder_->path(), encoder_->payload().size());

    if (collector_cluster_.exists()) {
      Http::AsyncClient::Request* request =
          driver_.clusterManager()
              .httpAsyncClientForCluster(collector_cluster_.info()->name())
              .send(
                  std::move(message), *this,
                  Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(1000U)));
      if (request) {
        active_requests_.add(*request);
      }
    } else {
      ENVOY_LOG(debug, "collector cluster '{}' does not exist", driver_.cluster());
      driver_.tracerStats().reports_skipped_no_cluster_.inc();
    }

    encoder_->clearTraces();
  }
}

void TraceReporter::onFailure(const Http::AsyncClient::Request& request,
                              Http::AsyncClient::FailureReason) {
  active_requests_.remove(request);
  ENVOY_LOG(debug, "failure submitting traces to datadog agent");
  driver_.tracerStats().reports_failed_.inc();
}

void TraceReporter::onSuccess(const Http::AsyncClient::Request& request,
                              Http::ResponseMessagePtr&& http_response) {
  active_requests_.remove(request);
  uint64_t responseStatus = Http::Utility::getResponseStatus(http_response->headers());
  if (responseStatus != enumToInt(Http::Code::OK)) {
    // TODO: Consider adding retries for failed submissions.
    ENVOY_LOG(debug, "unexpected HTTP response code from datadog agent: {}", responseStatus);
    driver_.tracerStats().reports_dropped_.inc();
  } else {
    ENVOY_LOG(debug, "traces successfully submitted to datadog agent");
    driver_.tracerStats().reports_sent_.inc();
    encoder_->handleResponse(http_response->body()->toString());
  }
}

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
