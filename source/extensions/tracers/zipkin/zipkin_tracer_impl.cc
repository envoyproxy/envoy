#include "source/extensions/tracers/zipkin/zipkin_tracer_impl.h"

#include "envoy/config/trace/v3/zipkin.pb.h"

#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/fmt.h"
#include "source/common/common/utility.h"
#include "source/common/config/utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/extensions/tracers/zipkin/span_context_extractor.h"
#include "source/extensions/tracers/zipkin/zipkin_core_constants.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

ZipkinSpan::ZipkinSpan(Zipkin::Span& span, Zipkin::Tracer& tracer) : span_(span), tracer_(tracer) {}

void ZipkinSpan::finishSpan() { span_.finish(); }

void ZipkinSpan::setOperation(absl::string_view operation) {
  span_.setName(std::string(operation));
}

void ZipkinSpan::setTag(absl::string_view name, absl::string_view value) {
  span_.setTag(name, value);
}

void ZipkinSpan::log(SystemTime timestamp, const std::string& event) {
  span_.log(timestamp, event);
}

// TODO(#11622): Implement baggage storage for zipkin spans
void ZipkinSpan::setBaggage(absl::string_view, absl::string_view) {}
std::string ZipkinSpan::getBaggage(absl::string_view) { return EMPTY_STRING; }

void ZipkinSpan::injectContext(Tracing::TraceContext& trace_context,
                               const Upstream::HostDescriptionConstSharedPtr&) {
  // Set the trace-id and span-id headers properly, based on the newly-created span structure.
  trace_context.setByReferenceKey(ZipkinCoreConstants::get().X_B3_TRACE_ID,
                                  span_.traceIdAsHexString());
  trace_context.setByReferenceKey(ZipkinCoreConstants::get().X_B3_SPAN_ID, span_.idAsHexString());

  // Set the parent-span header properly, based on the newly-created span structure.
  if (span_.isSetParentId()) {
    trace_context.setByReferenceKey(ZipkinCoreConstants::get().X_B3_PARENT_SPAN_ID,
                                    span_.parentIdAsHexString());
  }

  // Set the sampled header.
  trace_context.setByReferenceKey(ZipkinCoreConstants::get().X_B3_SAMPLED,
                                  span_.sampled() ? SAMPLED : NOT_SAMPLED);
}

void ZipkinSpan::setSampled(bool sampled) { span_.setSampled(sampled); }

Tracing::SpanPtr ZipkinSpan::spawnChild(const Tracing::Config& config, const std::string& name,
                                        SystemTime start_time) {
  SpanContext previous_context(span_);
  return std::make_unique<ZipkinSpan>(
      *tracer_.startSpan(config, name, start_time, previous_context), tracer_);
}

Driver::TlsTracer::TlsTracer(TracerPtr&& tracer, Driver& driver)
    : tracer_(std::move(tracer)), driver_(driver) {}

Driver::Driver(const envoy::config::trace::v3::ZipkinConfig& zipkin_config,
               Upstream::ClusterManager& cluster_manager, Stats::Scope& scope,
               ThreadLocal::SlotAllocator& tls, Runtime::Loader& runtime,
               const LocalInfo::LocalInfo& local_info, Random::RandomGenerator& random_generator,
               TimeSource& time_source)
    : cm_(cluster_manager), tracer_stats_{ZIPKIN_TRACER_STATS(
                                POOL_COUNTER_PREFIX(scope, "tracing.zipkin."))},
      tls_(tls.allocateSlot()), runtime_(runtime), local_info_(local_info),
      time_source_(time_source) {
  Config::Utility::checkCluster("envoy.tracers.zipkin", zipkin_config.collector_cluster(), cm_,
                                /* allow_added_via_api */ true);
  cluster_ = zipkin_config.collector_cluster();
  hostname_ = !zipkin_config.collector_hostname().empty() ? zipkin_config.collector_hostname()
                                                          : zipkin_config.collector_cluster();

  CollectorInfo collector;
  if (!zipkin_config.collector_endpoint().empty()) {
    collector.endpoint_ = zipkin_config.collector_endpoint();
  }
  // The current default version of collector_endpoint_version is HTTP_JSON.
  collector.version_ = zipkin_config.collector_endpoint_version();
  const bool trace_id_128bit = zipkin_config.trace_id_128bit();

  const bool shared_span_context = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      zipkin_config, shared_span_context, DEFAULT_SHARED_SPAN_CONTEXT);
  collector.shared_span_context_ = shared_span_context;

  const bool split_spans_for_request = zipkin_config.split_spans_for_request();

  tls_->set([this, collector, &random_generator, trace_id_128bit, shared_span_context,
             split_spans_for_request](
                Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    TracerPtr tracer = std::make_unique<Tracer>(
        local_info_.clusterName(), local_info_.address(), random_generator, trace_id_128bit,
        shared_span_context, time_source_, split_spans_for_request);
    tracer->setReporter(
        ReporterImpl::NewInstance(std::ref(*this), std::ref(dispatcher), collector));
    return std::make_shared<TlsTracer>(std::move(tracer), *this);
  });
}

Tracing::SpanPtr Driver::startSpan(const Tracing::Config& config,
                                   Tracing::TraceContext& trace_context, const std::string&,
                                   SystemTime start_time,
                                   const Tracing::Decision tracing_decision) {
  Tracer& tracer = *tls_->getTyped<TlsTracer>().tracer_;
  SpanPtr new_zipkin_span;
  SpanContextExtractor extractor(trace_context);
  bool sampled{extractor.extractSampled(tracing_decision)};
  try {
    auto ret_span_context = extractor.extractSpanContext(sampled);
    if (!ret_span_context.second) {
      // Create a root Zipkin span. No context was found in the headers.
      new_zipkin_span =
          tracer.startSpan(config, std::string(trace_context.authority()), start_time);
      new_zipkin_span->setSampled(sampled);
    } else {
      new_zipkin_span = tracer.startSpan(config, std::string(trace_context.authority()), start_time,
                                         ret_span_context.first);
    }

  } catch (const ExtractorException& e) {
    return std::make_unique<Tracing::NullSpan>();
  }

  // Return the active Zipkin span.
  return std::make_unique<ZipkinSpan>(*new_zipkin_span, tracer);
}

ReporterImpl::ReporterImpl(Driver& driver, Event::Dispatcher& dispatcher,
                           const CollectorInfo& collector)
    : driver_(driver),
      collector_(collector), span_buffer_{std::make_unique<SpanBuffer>(
                                 collector.version_, collector.shared_span_context_)},
      collector_cluster_(driver_.clusterManager(), driver_.cluster()) {
  flush_timer_ = dispatcher.createTimer([this]() -> void {
    driver_.tracerStats().timer_flushed_.inc();
    flushSpans();
    enableTimer();
  });

  const uint64_t min_flush_spans =
      driver_.runtime().snapshot().getInteger("tracing.zipkin.min_flush_spans", 5U);
  span_buffer_->allocateBuffer(min_flush_spans);

  enableTimer();
}

ReporterPtr ReporterImpl::NewInstance(Driver& driver, Event::Dispatcher& dispatcher,
                                      const CollectorInfo& collector) {
  return std::make_unique<ReporterImpl>(driver, dispatcher, collector);
}

void ReporterImpl::reportSpan(Span&& span) {
  span_buffer_->addSpan(std::move(span));

  const uint64_t min_flush_spans =
      driver_.runtime().snapshot().getInteger("tracing.zipkin.min_flush_spans", 5U);

  if (span_buffer_->pendingSpans() == min_flush_spans) {
    flushSpans();
  }
}

void ReporterImpl::enableTimer() {
  const uint64_t flush_interval =
      driver_.runtime().snapshot().getInteger("tracing.zipkin.flush_interval_ms", 5000U);
  flush_timer_->enableTimer(std::chrono::milliseconds(flush_interval));
}

void ReporterImpl::flushSpans() {
  if (span_buffer_->pendingSpans()) {
    driver_.tracerStats().spans_sent_.add(span_buffer_->pendingSpans());
    const std::string request_body = span_buffer_->serialize();
    Http::RequestMessagePtr message = std::make_unique<Http::RequestMessageImpl>();
    message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Post);
    message->headers().setPath(collector_.endpoint_);
    message->headers().setHost(driver_.hostname());
    message->headers().setReferenceContentType(
        collector_.version_ == envoy::config::trace::v3::ZipkinConfig::HTTP_PROTO
            ? Http::Headers::get().ContentTypeValues.Protobuf
            : Http::Headers::get().ContentTypeValues.Json);

    message->body().add(request_body);

    const uint64_t timeout =
        driver_.runtime().snapshot().getInteger("tracing.zipkin.request_timeout", 5000U);

    if (collector_cluster_.threadLocalCluster().has_value()) {
      Http::AsyncClient::Request* request =
          collector_cluster_.threadLocalCluster()->get().httpAsyncClient().send(
              std::move(message), *this,
              Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(timeout)));
      if (request) {
        active_requests_.add(*request);
      }
    } else {
      ENVOY_LOG(debug, "collector cluster '{}' does not exist", driver_.cluster());
      driver_.tracerStats().reports_skipped_no_cluster_.inc();
    }

    span_buffer_->clear();
  }
}

void ReporterImpl::onFailure(const Http::AsyncClient::Request& request,
                             Http::AsyncClient::FailureReason) {
  active_requests_.remove(request);
  driver_.tracerStats().reports_failed_.inc();
}

void ReporterImpl::onSuccess(const Http::AsyncClient::Request& request,
                             Http::ResponseMessagePtr&& http_response) {
  active_requests_.remove(request);
  if (Http::Utility::getResponseStatus(http_response->headers()) !=
      enumToInt(Http::Code::Accepted)) {
    driver_.tracerStats().reports_dropped_.inc();
  } else {
    driver_.tracerStats().reports_sent_.inc();
  }
}

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
