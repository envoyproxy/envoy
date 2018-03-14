#include "common/tracing/zipkin/zipkin_tracer_impl.h"

#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/tracing/http_tracer_impl.h"
#include "common/tracing/zipkin/zipkin_core_constants.h"

namespace Envoy {
namespace Zipkin {

ZipkinSpan::ZipkinSpan(Zipkin::Span& span, Zipkin::Tracer& tracer) : span_(span), tracer_(tracer) {}

void ZipkinSpan::finishSpan() { span_.finish(); }

void ZipkinSpan::setOperation(const std::string& operation) { span_.setName(operation); }

void ZipkinSpan::setTag(const std::string& name, const std::string& value) {
  span_.setTag(name, value);
}

void ZipkinSpan::injectContext(Http::HeaderMap& request_headers) {
  // Set the trace-id and span-id headers properly, based on the newly-created span structure.
  request_headers.insertXB3TraceId().value(span_.traceIdAsHexString());
  request_headers.insertXB3SpanId().value(span_.idAsHexString());

  // Set the parent-span header properly, based on the newly-created span structure.
  if (span_.isSetParentId()) {
    request_headers.insertXB3ParentSpanId().value(span_.parentIdAsHexString());
  }

  // Set the sampled header.
  request_headers.insertXB3Sampled().value().setReference(
      span_.sampled() ? ZipkinCoreConstants::get().SAMPLED
                      : ZipkinCoreConstants::get().NOT_SAMPLED);
}

Tracing::SpanPtr ZipkinSpan::spawnChild(const Tracing::Config& config, const std::string& name,
                                        SystemTime start_time) {
  SpanContext context(span_);
  return Tracing::SpanPtr{
      new ZipkinSpan(*tracer_.startSpan(config, name, start_time, context), tracer_)};
}

Driver::TlsTracer::TlsTracer(TracerPtr&& tracer, Driver& driver)
    : tracer_(std::move(tracer)), driver_(driver) {}

Driver::Driver(const Json::Object& config, Upstream::ClusterManager& cluster_manager,
               Stats::Store& stats, ThreadLocal::SlotAllocator& tls, Runtime::Loader& runtime,
               const LocalInfo::LocalInfo& local_info, Runtime::RandomGenerator& random_generator)
    : cm_(cluster_manager), tracer_stats_{ZIPKIN_TRACER_STATS(
                                POOL_COUNTER_PREFIX(stats, "tracing.zipkin."))},
      tls_(tls.allocateSlot()), runtime_(runtime), local_info_(local_info) {

  Upstream::ThreadLocalCluster* cluster = cm_.get(config.getString("collector_cluster"));
  if (!cluster) {
    throw EnvoyException(fmt::format("{} collector cluster is not defined on cluster manager level",
                                     config.getString("collector_cluster")));
  }
  cluster_ = cluster->info();

  const std::string collector_endpoint =
      config.getString("collector_endpoint", ZipkinCoreConstants::get().DEFAULT_COLLECTOR_ENDPOINT);

  tls_->set([this, collector_endpoint, &random_generator](
                Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    TracerPtr tracer(
        new Tracer(local_info_.clusterName(), local_info_.address(), random_generator));
    tracer->setReporter(
        ReporterImpl::NewInstance(std::ref(*this), std::ref(dispatcher), collector_endpoint));
    return ThreadLocal::ThreadLocalObjectSharedPtr{new TlsTracer(std::move(tracer), *this)};
  });
}

Tracing::SpanPtr Driver::startSpan(const Tracing::Config& config, Http::HeaderMap& request_headers,
                                   const std::string&, SystemTime start_time) {
  Tracer& tracer = *tls_->getTyped<TlsTracer>().tracer_;
  SpanPtr new_zipkin_span;
  bool sampled(true);

  if (request_headers.XB3Sampled()) {
    // Checking if sampled flag has been specified. Also checking for 'true' value, as some old
    // zipkin tracers may still use that value, although should be 0 or 1.
    sampled =
        request_headers.XB3Sampled()->value().getString() == ZipkinCoreConstants::get().SAMPLED ||
        request_headers.XB3Sampled()->value().getString() == "true";
  }

  if (request_headers.XB3TraceId() && request_headers.XB3SpanId()) {
    uint64_t trace_id(0);
    uint64_t span_id(0);
    uint64_t parent_id(0);
    if (!StringUtil::atoul(request_headers.XB3TraceId()->value().c_str(), trace_id, 16) ||
        !StringUtil::atoul(request_headers.XB3SpanId()->value().c_str(), span_id, 16) ||
        (request_headers.XB3ParentSpanId() &&
         !StringUtil::atoul(request_headers.XB3ParentSpanId()->value().c_str(), parent_id, 16))) {
      return Tracing::SpanPtr(new Tracing::NullSpan());
    }

    SpanContext context(trace_id, span_id, parent_id, sampled);

    new_zipkin_span =
        tracer.startSpan(config, request_headers.Host()->value().c_str(), start_time, context);
  } else {
    // Create a root Zipkin span. No context was found in the headers.
    new_zipkin_span = tracer.startSpan(config, request_headers.Host()->value().c_str(), start_time);
    new_zipkin_span->setSampled(sampled);
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
} // namespace Envoy
