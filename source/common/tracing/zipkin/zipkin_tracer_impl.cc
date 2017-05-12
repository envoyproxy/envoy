#include "common/tracing/zipkin/zipkin_tracer_impl.h"

#include "common/common/enum_to_int.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/tracing/http_tracer_impl.h"
#include "common/tracing/zipkin/zipkin_core_constants.h"

#include "spdlog/spdlog.h"

namespace Zipkin {

ZipkinSpan::ZipkinSpan(Zipkin::Span& span) : span_(span) {}

void ZipkinSpan::finishSpan() { span_.finish(); }

void ZipkinSpan::setTag(const std::string& name, const std::string& value) {
  if (this->hasCSAnnotation()) {
    span_.setTag(name, value);
  }
}

bool ZipkinSpan::hasCSAnnotation() {
  auto annotations = span_.annotations();
  if (annotations.size() > 0) {
    // We currently expect only one annotation to be in the span when this function is called.
    return annotations[0].value() == ZipkinCoreConstants::get().CLIENT_SEND;
  }
  return false;
}

ZipkinDriver::TlsZipkinTracer::TlsZipkinTracer(TracerPtr tracer, ZipkinDriver& driver)
    : tracer_(std::move(tracer)), driver_(driver) {}

ZipkinDriver::ZipkinDriver(const Json::Object& config, Upstream::ClusterManager& cluster_manager,
                           Stats::Store& stats, ThreadLocal::Instance& tls,
                           Runtime::Loader& runtime, const LocalInfo::LocalInfo& local_info,
                           Runtime::RandomGenerator& random_generator)
    : cm_(cluster_manager),
      tracer_stats_{ZIPKIN_TRACER_STATS(POOL_COUNTER_PREFIX(stats, "tracing.zipkin."))}, tls_(tls),
      runtime_(runtime), local_info_(local_info), tls_slot_(tls.allocateSlot()) {

  Upstream::ThreadLocalCluster* cluster = cm_.get(config.getString("collector_cluster"));
  if (!cluster) {
    throw EnvoyException(fmt::format("{} collector cluster is not defined on cluster manager level",
                                     config.getString("collector_cluster")));
  }
  cluster_ = cluster->info();

  const std::string collector_endpoint =
      config.getString("collector_endpoint", ZipkinCoreConstants::get().DEFAULT_COLLECTOR_ENDPOINT);

  tls_.set(tls_slot_, [this, collector_endpoint, &random_generator](Event::Dispatcher& dispatcher)
                          -> ThreadLocal::ThreadLocalObjectSharedPtr {
                            TracerPtr tracer(new Tracer(local_info_.clusterName(),
                                                        local_info_.address(), random_generator));
                            tracer->setReporter(ZipkinReporter::NewInstance(
                                std::ref(*this), std::ref(dispatcher), collector_endpoint));
                            return ThreadLocal::ThreadLocalObjectSharedPtr{
                                new TlsZipkinTracer(std::move(tracer), *this)};
                          });
}

Tracing::SpanPtr ZipkinDriver::startSpan(Http::HeaderMap& request_headers, const std::string&,
                                         SystemTime start_time) {
  Tracer& tracer = *tls_.getTyped<TlsZipkinTracer>(tls_slot_).tracer_;
  SpanPtr new_zipkin_span;

  if (request_headers.OtSpanContext()) {
    // Get the open-tracing span context.
    // This header contains a span's parent-child relationships set by the downstream Envoy.
    // The context built from this header allows the Zipkin tracer to
    // properly set the span id and the parent span id.
    SpanContext context;

    context.populateFromString(request_headers.OtSpanContext()->value().c_str());

    // Create either a child or a shared-context Zipkin span.
    //
    // An all-new child span will be started if the current context carries the SR annotation. In
    // this case, we are dealing with an egress operation that causally succeeds a previous
    // ingress operation. This envoy instance will be the the client-side of the new span, to which
    // it will add the CS annotation.
    //
    // Differently, a shared-context span will be created if the current context carries the CS
    // annotation. In this case, we are dealing with an ingress operation. This envoy instance,
    // being at the receiving end, will add the SR annotation to the shared span context.

    new_zipkin_span =
        tracer.startSpan(request_headers.Host()->value().c_str(), start_time, context);
  } else {
    // Create a root Zipkin span. No context was found in the headers.
    new_zipkin_span = tracer.startSpan(request_headers.Host()->value().c_str(), start_time);
  }

  // Set the trace-id and span-id headers properly, based on the newly-created span structure.
  request_headers.insertXB3TraceId().value(new_zipkin_span->traceIdAsHexString());
  request_headers.insertXB3SpanId().value(new_zipkin_span->idAsHexString());

  // Set the parent-span header properly, based on the newly-created span structure.
  if (new_zipkin_span->isSetParentId()) {
    request_headers.insertXB3ParentSpanId().value(new_zipkin_span->parentIdAsHexString());
  }

  // Set the sampled header.
  request_headers.insertXB3Sampled().value(ZipkinCoreConstants::get().ALWAYS_SAMPLE);

  // Set the ot-span-context header with the new context.
  SpanContext new_span_context(*new_zipkin_span);
  request_headers.insertOtSpanContext().value(new_span_context.serializeToString());

  ZipkinSpanPtr active_span;
  active_span.reset(new ZipkinSpan(*new_zipkin_span));

  return std::move(active_span);
}

ZipkinReporter::ZipkinReporter(ZipkinDriver& driver, Event::Dispatcher& dispatcher,
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

ReporterPtr ZipkinReporter::NewInstance(ZipkinDriver& driver, Event::Dispatcher& dispatcher,
                                        const std::string& collector_endpoint) {
  return std::unique_ptr<Reporter>(new ZipkinReporter(driver, dispatcher, collector_endpoint));
}

void ZipkinReporter::reportSpan(Span&& span) {
  span_buffer_.addSpan(std::move(span));

  const uint64_t min_flush_spans =
      driver_.runtime().snapshot().getInteger("tracing.zipkin.min_flush_spans", 5U);

  if (span_buffer_.pendingSpans() == min_flush_spans) {
    flushSpans();
  }
}

void ZipkinReporter::enableTimer() {
  const uint64_t flush_interval =
      driver_.runtime().snapshot().getInteger("tracing.zipkin.flush_interval_ms", 5000U);
  flush_timer_->enableTimer(std::chrono::milliseconds(flush_interval));
}

void ZipkinReporter::flushSpans() {
  if (span_buffer_.pendingSpans()) {
    driver_.tracerStats().spans_sent_.add(span_buffer_.pendingSpans());

    const std::string request_body = span_buffer_.toStringifiedJsonArray();
    Http::MessagePtr message(new Http::RequestMessageImpl());
    message->headers().insertMethod().value(Http::Headers::get().MethodValues.Post);
    message->headers().insertPath().value(collector_endpoint_);
    message->headers().insertHost().value(driver_.cluster()->name());
    message->headers().insertContentType().value(std::string("application/json"));

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

void ZipkinReporter::onFailure(Http::AsyncClient::FailureReason) {
  driver_.tracerStats().reports_failed_.inc();
}

void ZipkinReporter::onSuccess(Http::MessagePtr&& http_response) {
  if (Http::Utility::getResponseStatus(http_response->headers()) !=
      enumToInt(Http::Code::Accepted)) {
    driver_.tracerStats().reports_dropped_.inc();
  } else {
    driver_.tracerStats().reports_sent_.inc();
  }
}
} // Zipkin
