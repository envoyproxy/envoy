#include "common/tracing/lightstep_tracer_impl.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "common/common/base64.h"
#include "common/grpc/common.h"
#include "common/http/message_impl.h"
#include "common/tracing/http_tracer_impl.h"

#include "fmt/format.h"

namespace Envoy {
namespace Tracing {

LightStepSpan::LightStepSpan(lightstep::Span& span, lightstep::Tracer& tracer)
    : span_(span), tracer_(tracer) {}

void LightStepSpan::finishSpan() { span_.Finish(); }

void LightStepSpan::setOperation(const std::string& operation) {
  span_.SetOperationName(operation);
}

void LightStepSpan::setTag(const std::string& name, const std::string& value) {
  span_.SetTag(name, value);
}

void LightStepSpan::injectContext(Http::HeaderMap& request_headers) {
  lightstep::BinaryCarrier ctx;
  tracer_.Inject(context(), lightstep::CarrierFormat::LightStepBinaryCarrier,
                 lightstep::ProtoWriter(&ctx));
  const std::string current_span_context = ctx.SerializeAsString();
  request_headers.insertOtSpanContext().value(
      Base64::encode(current_span_context.c_str(), current_span_context.length()));
}

SpanPtr LightStepSpan::spawnChild(const Config&, const std::string& name, SystemTime start_time) {
  lightstep::Span ls_span = tracer_.StartSpan(
      name, {lightstep::ChildOf(span_.context()), lightstep::StartTimestamp(start_time)});
  return SpanPtr{new LightStepSpan(ls_span, tracer_)};
}

LightStepRecorder::LightStepRecorder(const lightstep::TracerImpl& tracer, LightStepDriver& driver,
                                     Event::Dispatcher& dispatcher)
    : builder_(tracer), driver_(driver) {
  flush_timer_ = dispatcher.createTimer([this]() -> void {
    driver_.tracerStats().timer_flushed_.inc();
    flushSpans();
    enableTimer();
  });

  enableTimer();
}

void LightStepRecorder::RecordSpan(lightstep::collector::Span&& span) {
  builder_.addSpan(std::move(span));

  uint64_t min_flush_spans =
      driver_.runtime().snapshot().getInteger("tracing.lightstep.min_flush_spans", 5U);
  if (builder_.pendingSpans() == min_flush_spans) {
    flushSpans();
  }
}

bool LightStepRecorder::FlushWithTimeout(lightstep::Duration) {
  // Note: We don't expect this to be called, since the Tracer
  // reference is private to its LightStepSink.
  return true;
}

std::unique_ptr<lightstep::Recorder>
LightStepRecorder::NewInstance(LightStepDriver& driver, Event::Dispatcher& dispatcher,
                               const lightstep::TracerImpl& tracer) {
  return std::unique_ptr<lightstep::Recorder>(new LightStepRecorder(tracer, driver, dispatcher));
}

void LightStepRecorder::enableTimer() {
  uint64_t flush_interval =
      driver_.runtime().snapshot().getInteger("tracing.lightstep.flush_interval_ms", 1000U);
  flush_timer_->enableTimer(std::chrono::milliseconds(flush_interval));
}

void LightStepRecorder::flushSpans() {
  if (builder_.pendingSpans() != 0) {
    driver_.tracerStats().spans_sent_.add(builder_.pendingSpans());
    lightstep::collector::ReportRequest request;
    std::swap(request, builder_.pending());

    Http::MessagePtr message = Grpc::Common::prepareHeaders(driver_.cluster()->name(),
                                                            lightstep::CollectorServiceFullName(),
                                                            lightstep::CollectorMethodName());

    message->body() = Grpc::Common::serializeBody(std::move(request));

    uint64_t timeout =
        driver_.runtime().snapshot().getInteger("tracing.lightstep.request_timeout", 5000U);
    driver_.clusterManager()
        .httpAsyncClientForCluster(driver_.cluster()->name())
        .send(std::move(message), *this, std::chrono::milliseconds(timeout));
  }
}

LightStepDriver::TlsLightStepTracer::TlsLightStepTracer(lightstep::Tracer tracer,
                                                        LightStepDriver& driver)
    : tracer_(new lightstep::Tracer(tracer)), driver_(driver) {}

LightStepDriver::LightStepDriver(const Json::Object& config,
                                 Upstream::ClusterManager& cluster_manager, Stats::Store& stats,
                                 ThreadLocal::SlotAllocator& tls, Runtime::Loader& runtime,
                                 std::unique_ptr<lightstep::TracerOptions> options)
    : cm_(cluster_manager), tracer_stats_{LIGHTSTEP_TRACER_STATS(
                                POOL_COUNTER_PREFIX(stats, "tracing.lightstep."))},
      tls_(tls.allocateSlot()), runtime_(runtime), options_(std::move(options)) {
  Upstream::ThreadLocalCluster* cluster = cm_.get(config.getString("collector_cluster"));
  if (!cluster) {
    throw EnvoyException(fmt::format("{} collector cluster is not defined on cluster manager level",
                                     config.getString("collector_cluster")));
  }
  cluster_ = cluster->info();

  if (!(cluster_->features() & Upstream::ClusterInfo::Features::HTTP2)) {
    throw EnvoyException(
        fmt::format("{} collector cluster must support http2 for gRPC calls", cluster_->name()));
  }

  tls_->set([this](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    lightstep::Tracer tracer(lightstep::NewUserDefinedTransportLightStepTracer(
        *options_, std::bind(&LightStepRecorder::NewInstance, std::ref(*this), std::ref(dispatcher),
                             std::placeholders::_1)));

    return ThreadLocal::ThreadLocalObjectSharedPtr{
        new TlsLightStepTracer(std::move(tracer), *this)};
  });
}

SpanPtr LightStepDriver::startSpan(const Config&, Http::HeaderMap& request_headers,
                                   const std::string& operation_name, SystemTime start_time) {
  lightstep::Tracer& tracer = *tls_->getTyped<TlsLightStepTracer>().tracer_;
  LightStepSpanPtr active_span;

  if (request_headers.OtSpanContext()) {
    // Extract downstream context from HTTP carrier.
    // This code is safe even if decode returns empty string or data is malformed.
    std::string parent_context = Base64::decode(request_headers.OtSpanContext()->value().c_str());
    lightstep::BinaryCarrier ctx;
    ctx.ParseFromString(parent_context);

    lightstep::SpanContext parent_span_ctx = tracer.Extract(
        lightstep::CarrierFormat::LightStepBinaryCarrier, lightstep::ProtoReader(ctx));
    lightstep::Span ls_span =
        tracer.StartSpan(operation_name, {lightstep::ChildOf(parent_span_ctx),
                                          lightstep::StartTimestamp(start_time)});
    active_span.reset(new LightStepSpan(ls_span, tracer));
  } else {
    lightstep::Span ls_span =
        tracer.StartSpan(operation_name, {lightstep::StartTimestamp(start_time)});
    active_span.reset(new LightStepSpan(ls_span, tracer));
  }

  return std::move(active_span);
}

void LightStepRecorder::onFailure(Http::AsyncClient::FailureReason) {
  Grpc::Common::chargeStat(*driver_.cluster(), lightstep::CollectorServiceFullName(),
                           lightstep::CollectorMethodName(), false);
}

void LightStepRecorder::onSuccess(Http::MessagePtr&& msg) {
  try {
    Grpc::Common::validateResponse(*msg);

    Grpc::Common::chargeStat(*driver_.cluster(), lightstep::CollectorServiceFullName(),
                             lightstep::CollectorMethodName(), true);
  } catch (const Grpc::Exception& ex) {
    Grpc::Common::chargeStat(*driver_.cluster(), lightstep::CollectorServiceFullName(),
                             lightstep::CollectorMethodName(), false);
  }
}

} // namespace Tracing
} // namespace Envoy
