#include "extensions/tracers/zipkin/zipkin_tracer_impl.h"

#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/config/utility.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/well_known_names.h"
#include "extensions/tracers/zipkin/span_context_extractor.h"
#include "extensions/tracers/zipkin/zipkin_core_constants.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

ZipkinSpan::ZipkinSpan(Zipkin::Span& span, Zipkin::Tracer& tracer) : span_(span), tracer_(tracer) {}

void ZipkinSpan::finishSpan() { span_.finish(); }

void ZipkinSpan::setOperation(const std::string& operation) { span_.setName(operation); }

void ZipkinSpan::setTag(const std::string& name, const std::string& value) {
  span_.setTag(name, value);
}

void ZipkinSpan::injectContext(Http::HeaderMap& request_headers) {
  // Set the trace-id and span-id headers properly, based on the newly-created span structure.
  request_headers.addReferenceKey(ZipkinCoreConstants::get().X_B3_TRACE_ID,
                                  span_.traceIdAsHexString());
  request_headers.addReferenceKey(ZipkinCoreConstants::get().X_B3_SPAN_ID, span_.idAsHexString());

  // Set the parent-span header properly, based on the newly-created span structure.
  if (span_.isSetParentId()) {
    request_headers.addReferenceKey(ZipkinCoreConstants::get().X_B3_PARENT_SPAN_ID,
                                    span_.parentIdAsHexString());
  }

  // Set the sampled header.
  request_headers.addReferenceKey(ZipkinCoreConstants::get().X_B3_SAMPLED,
                                  span_.sampled() ? ZipkinCoreConstants::get().SAMPLED
                                                  : ZipkinCoreConstants::get().NOT_SAMPLED);
}

void ZipkinSpan::setSampled(bool sampled) { span_.setSampled(sampled); }

Tracing::SpanPtr ZipkinSpan::spawnChild(const Tracing::Config& config, const std::string& name,
                                        SystemTime start_time) {
  SpanContext context(span_);
  return Tracing::SpanPtr{
      new ZipkinSpan(*tracer_.startSpan(config, name, start_time, context), tracer_)};
}

Driver::TlsTracer::TlsTracer(TracerPtr&& tracer, Driver& driver)
    : tracer_(std::move(tracer)), driver_(driver) {}

Driver::Driver(const envoy::config::trace::v2::ZipkinConfig& zipkin_config,
               Upstream::ClusterManager& cluster_manager, Stats::Store& stats,
               ThreadLocal::SlotAllocator& tls, Runtime::Loader& runtime,
               const LocalInfo::LocalInfo& local_info, Runtime::RandomGenerator& random_generator,
               TimeSource& time_source)
    : cm_(cluster_manager), tracer_stats_{ZIPKIN_TRACER_STATS(
                                POOL_COUNTER_PREFIX(stats, "tracing.zipkin."))},
      tls_(tls.allocateSlot()), runtime_(runtime), local_info_(local_info),
      time_source_(time_source) {
  Config::Utility::checkCluster(TracerNames::get().Zipkin, zipkin_config.collector_cluster(), cm_);
  cluster_ = cm_.get(zipkin_config.collector_cluster())->info();

  std::string collector_endpoint = ZipkinCoreConstants::get().DEFAULT_COLLECTOR_ENDPOINT;
  if (zipkin_config.collector_endpoint().size() > 0) {
    collector_endpoint = zipkin_config.collector_endpoint();
  }

  const bool trace_id_128bit = zipkin_config.trace_id_128bit();

  const bool shared_span_context = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      zipkin_config, shared_span_context, ZipkinCoreConstants::get().DEFAULT_SHARED_SPAN_CONTEXT);

  tls_->set([this, collector_endpoint, &random_generator, trace_id_128bit, shared_span_context](
                Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    TracerPtr tracer(new Tracer(local_info_.clusterName(), local_info_.address(), random_generator,
                                trace_id_128bit, shared_span_context, time_source_));
    tracer->setReporter(
        ReporterImpl::NewInstance(std::ref(*this), std::ref(dispatcher), collector_endpoint));
    return ThreadLocal::ThreadLocalObjectSharedPtr{new TlsTracer(std::move(tracer), *this)};
  });
}

Tracing::SpanPtr Driver::startSpan(const Tracing::Config& config, Http::HeaderMap& request_headers,
                                   const std::string&, SystemTime start_time,
                                   const Tracing::Decision tracing_decision) {
  Tracer& tracer = *tls_->getTyped<TlsTracer>().tracer_;
  SpanPtr new_zipkin_span;
  SpanContextExtractor extractor(request_headers);
  bool sampled{extractor.extractSampled(tracing_decision)};
  try {
    auto ret_span_context = extractor.extractSpanContext(sampled);
    if (!ret_span_context.second) {
      // Create a root Zipkin span. No context was found in the headers.
      new_zipkin_span =
          tracer.startSpan(config, request_headers.Host()->value().c_str(), start_time);
      new_zipkin_span->setSampled(sampled);
    } else {
      new_zipkin_span = tracer.startSpan(config, request_headers.Host()->value().c_str(),
                                         start_time, ret_span_context.first);
    }

  } catch (const ExtractorException& e) {
    return Tracing::SpanPtr(new Tracing::NullSpan());
  }

  ZipkinSpanPtr active_span(new ZipkinSpan(*new_zipkin_span, tracer));
  return std::move(active_span);
}

ReporterImpl::ReporterImpl(Driver& driver, Event::Dispatcher& dispatcher,
                           const std::string& collector_endpoint)
    : driver_(driver), collector_endpoint_(collector_endpoint) {
  flush_timer_ = dispatcher.createTimer([this]() -> void {
    driver_.tracerStats().timer_flushed_.inc();
    flushSpans();
    enableTimer();
  });

  const uint64_t min_flush_spans =
      driver_.runtime().snapshot().getInteger("tracing.zipkin.min_flush_spans", 5U);
  span_buffer_.allocateBuffer(min_flush_spans);

  enableTimer();
}

ReporterPtr ReporterImpl::NewInstance(Driver& driver, Event::Dispatcher& dispatcher,
                                      const std::string& collector_endpoint) {
  return ReporterPtr(new ReporterImpl(driver, dispatcher, collector_endpoint));
}

// TODO(fabolive): Need to avoid the copy to improve performance.
void ReporterImpl::reportSpan(const Span& span) {
  span_buffer_.addSpan(span);

  const uint64_t min_flush_spans =
      driver_.runtime().snapshot().getInteger("tracing.zipkin.min_flush_spans", 5U);

  if (span_buffer_.pendingSpans() == min_flush_spans) {
    flushSpans();
  }
}

void ReporterImpl::enableTimer() {
  const uint64_t flush_interval =
      driver_.runtime().snapshot().getInteger("tracing.zipkin.flush_interval_ms", 5000U);
  flush_timer_->enableTimer(std::chrono::milliseconds(flush_interval));
}

void ReporterImpl::flushSpans() {
  if (span_buffer_.pendingSpans()) {
    driver_.tracerStats().spans_sent_.add(span_buffer_.pendingSpans());

    const std::string request_body = span_buffer_.toStringifiedJsonArray();
    Http::MessagePtr message(new Http::RequestMessageImpl());
    message->headers().insertMethod().value().setReference(Http::Headers::get().MethodValues.Post);
    message->headers().insertPath().value(collector_endpoint_);
    message->headers().insertHost().value(driver_.cluster()->name());
    message->headers().insertContentType().value().setReference(
        Http::Headers::get().ContentTypeValues.Json);

    Buffer::InstancePtr body(new Buffer::OwnedImpl());
    body->add(request_body);
    message->body() = std::move(body);

    const uint64_t timeout =
        driver_.runtime().snapshot().getInteger("tracing.zipkin.request_timeout", 5000U);
    driver_.clusterManager()
        .httpAsyncClientForCluster(driver_.cluster()->name())
        .send(std::move(message), *this, std::chrono::milliseconds(timeout));

    span_buffer_.clear();
  }
}

void ReporterImpl::onFailure(Http::AsyncClient::FailureReason) {
  driver_.tracerStats().reports_failed_.inc();
}

void ReporterImpl::onSuccess(Http::MessagePtr&& http_response) {
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
