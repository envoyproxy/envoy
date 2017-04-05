#include "common/tracing/http_tracer_impl.h"

#include "common/common/assert.h"
#include "common/common/base64.h"
#include "common/common/macros.h"
#include "common/common/utility.h"
#include "common/common/enum_to_int.h"
#include "common/grpc/common.h"
#include "common/http/access_log/access_log_formatter.h"
#include "common/http/codes.h"
#include "common/http/headers.h"
#include "common/http/header_map_impl.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/runtime/uuid_util.h"

#include "zipkin/zipkin_core_constants.h"
#include <iostream>

namespace Tracing {

static std::string buildRequestLine(const Http::HeaderMap& request_headers,
                                    const Http::AccessLog::RequestInfo& info) {
  std::string path = request_headers.EnvoyOriginalPath()
                         ? request_headers.EnvoyOriginalPath()->value().c_str()
                         : request_headers.Path()->value().c_str();
  static const size_t max_path_length = 128;

  if (path.length() > max_path_length) {
    path = path.substr(0, max_path_length);
  }

  return fmt::format("{} {} {}", request_headers.Method()->value().c_str(), path,
                     Http::AccessLog::AccessLogFormatUtils::protocolToString(info.protocol()));
}

static std::string buildResponseCode(const Http::AccessLog::RequestInfo& info) {
  return info.responseCode().valid() ? std::to_string(info.responseCode().value()) : "0";
}

static std::string valueOrDefault(const Http::HeaderEntry* header, const char* default_value) {
  return header ? header->value().c_str() : default_value;
}

void HttpTracerUtility::mutateHeaders(Http::HeaderMap& request_headers, Runtime::Loader& runtime) {
  if (!request_headers.RequestId()) {
    return;
  }

  // TODO PERF: Avoid copy.
  std::string x_request_id = request_headers.RequestId()->value().c_str();

  uint16_t result;
  // Skip if x-request-id is corrupted.
  if (!UuidUtils::uuidModBy(x_request_id, result, 10000)) {
    return;
  }

  // Do not apply tracing transformations if we are currently tracing.
  if (UuidTraceStatus::NoTrace == UuidUtils::isTraceableUuid(x_request_id)) {
    if (request_headers.ClientTraceId() &&
        runtime.snapshot().featureEnabled("tracing.client_enabled", 100)) {
      UuidUtils::setTraceableUuid(x_request_id, UuidTraceStatus::Client);
    } else if (request_headers.EnvoyForceTrace()) {
      UuidUtils::setTraceableUuid(x_request_id, UuidTraceStatus::Forced);
    } else if (runtime.snapshot().featureEnabled("tracing.random_sampling", 0, result, 10000)) {
      UuidUtils::setTraceableUuid(x_request_id, UuidTraceStatus::Sampled);
    }
  }

  if (!runtime.snapshot().featureEnabled("tracing.global_enabled", 100, result)) {
    UuidUtils::setTraceableUuid(x_request_id, UuidTraceStatus::NoTrace);
  }

  request_headers.RequestId()->value(x_request_id);
}

const std::string HttpTracerUtility::INGRESS_OPERATION = "ingress";
const std::string HttpTracerUtility::EGRESS_OPERATION = "egress";

const std::string& HttpTracerUtility::toString(OperationName operation_name) {
  switch (operation_name) {
  case OperationName::Ingress:
    return INGRESS_OPERATION;
  case OperationName::Egress:
    return EGRESS_OPERATION;
  }

  NOT_REACHED
}

Decision HttpTracerUtility::isTracing(const Http::AccessLog::RequestInfo& request_info,
                                      const Http::HeaderMap& request_headers) {
  // Exclude HC requests immediately.
  if (request_info.healthCheck()) {
    return {Reason::HealthCheck, false};
  }

  if (!request_headers.RequestId()) {
    return {Reason::NotTraceableRequestId, false};
  }

  // TODO PERF: Avoid copy.
  UuidTraceStatus trace_status =
      UuidUtils::isTraceableUuid(request_headers.RequestId()->value().c_str());

  switch (trace_status) {
  case UuidTraceStatus::Client:
    return {Reason::ClientForced, true};
  case UuidTraceStatus::Forced:
    return {Reason::ServiceForced, true};
  case UuidTraceStatus::Sampled:
    return {Reason::Sampling, true};
  case UuidTraceStatus::NoTrace:
    return {Reason::NotTraceableRequestId, false};
  }

  throw std::invalid_argument("Unknown trace_status");
}

void HttpTracerUtility::finalizeSpan(Span& active_span, const Http::HeaderMap& request_headers,
                                     const Http::AccessLog::RequestInfo& request_info) {
  // Pre response data.
  active_span.setTag("guid:x-request-id",
                     std::string(request_headers.RequestId()->value().c_str()));
  active_span.setTag("request_line", buildRequestLine(request_headers, request_info));
  active_span.setTag("request_size", std::to_string(request_info.bytesReceived()));
  active_span.setTag("host_header", valueOrDefault(request_headers.Host(), "-"));
  active_span.setTag("downstream_cluster",
                     valueOrDefault(request_headers.EnvoyDownstreamServiceCluster(), "-"));
  active_span.setTag("user_agent", valueOrDefault(request_headers.UserAgent(), "-"));

  if (request_headers.ClientTraceId()) {
    active_span.setTag("guid:x-client-trace-id",
                       std::string(request_headers.ClientTraceId()->value().c_str()));
  }

  // Post response data.
  active_span.setTag("response_code", buildResponseCode(request_info));
  active_span.setTag("response_size", std::to_string(request_info.bytesSent()));
  active_span.setTag("response_flags",
                     Http::AccessLog::ResponseFlagUtils::toShortString(request_info));

  if (request_info.responseCode().valid() &&
      Http::CodeUtility::is5xx(request_info.responseCode().value())) {
    active_span.setTag("error", "true");
  }

  active_span.finishSpan();
}

HttpTracerImpl::HttpTracerImpl(DriverPtr&& driver, const LocalInfo::LocalInfo& local_info)
    : driver_(std::move(driver)), local_info_(local_info) {}

SpanPtr HttpTracerImpl::startSpan(const Config& config, Http::HeaderMap& request_headers,
                                  const Http::AccessLog::RequestInfo& request_info) {
  std::string span_name = HttpTracerUtility::toString(config.operationName());

  if (config.operationName() == OperationName::Egress) {
    span_name.append(" ");
    span_name.append(request_headers.Host()->value().c_str());
  }

  SpanPtr active_span = driver_->startSpan(request_headers, span_name, request_info.startTime());
  if (active_span) {
    active_span->setTag("node_id", local_info_.nodeName());
    active_span->setTag("zone", local_info_.zoneName());
  }

  return active_span;
}

LightStepSpan::LightStepSpan(lightstep::Span& span) : span_(span) {}

void LightStepSpan::finishSpan() { span_.Finish(); }

void LightStepSpan::setTag(const std::string& name, const std::string& value) {
  span_.SetTag(name, value);
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
    : tracer_(tracer), driver_(driver) {}

LightStepDriver::LightStepDriver(const Json::Object& config,
                                 Upstream::ClusterManager& cluster_manager, Stats::Store& stats,
                                 ThreadLocal::Instance& tls, Runtime::Loader& runtime,
                                 std::unique_ptr<lightstep::TracerOptions> options)
    : cm_(cluster_manager),
      tracer_stats_{LIGHTSTEP_TRACER_STATS(POOL_COUNTER_PREFIX(stats, "tracing.lightstep."))},
      tls_(tls), runtime_(runtime), options_(std::move(options)), tls_slot_(tls.allocateSlot()) {
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

  tls_.set(tls_slot_,
           [this](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
             lightstep::Tracer tracer(lightstep::NewUserDefinedTransportLightStepTracer(
                 *options_, std::bind(&LightStepRecorder::NewInstance, std::ref(*this),
                                      std::ref(dispatcher), std::placeholders::_1)));

             return ThreadLocal::ThreadLocalObjectSharedPtr{
                 new TlsLightStepTracer(std::move(tracer), *this)};
           });
}

SpanPtr LightStepDriver::startSpan(Http::HeaderMap& request_headers,
                                   const std::string& operation_name, SystemTime start_time) {
  lightstep::Tracer& tracer = tls_.getTyped<TlsLightStepTracer>(tls_slot_).tracer_;
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
    active_span.reset(new LightStepSpan(ls_span));
  } else {
    lightstep::Span ls_span =
        tracer.StartSpan(operation_name, {lightstep::StartTimestamp(start_time)});
    active_span.reset(new LightStepSpan(ls_span));
  }

  // Inject newly created span context into HTTP carrier.
  lightstep::BinaryCarrier ctx;
  tracer.Inject(active_span->context(), lightstep::CarrierFormat::LightStepBinaryCarrier,
                lightstep::ProtoWriter(&ctx));
  const std::string current_span_context = ctx.SerializeAsString();
  request_headers.insertOtSpanContext().value(
      Base64::encode(current_span_context.c_str(), current_span_context.length()));

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
    return annotations[0].value() == Zipkin::ZipkinCoreConstants::CLIENT_SEND;
  }
  return false;
}

ZipkinDriver::TlsZipkinTracer::TlsZipkinTracer(Zipkin::Tracer tracer, ZipkinDriver& driver)
    : tracer_(tracer), driver_(driver) {}

ZipkinDriver::ZipkinDriver(const Json::Object& config, Upstream::ClusterManager& cluster_manager,
                           ThreadLocal::Instance& tls, Runtime::Loader& runtime,
                           const LocalInfo::LocalInfo& local_info)
    : cm_(cluster_manager), tls_(tls), runtime_(runtime), local_info_(local_info),
      tls_slot_(tls.allocateSlot()) {

  Upstream::ThreadLocalCluster* cluster = cm_.get(config.getString("collector_cluster"));
  if (!cluster) {
    throw EnvoyException(fmt::format("{} collector cluster is not defined on cluster manager level",
                                     config.getString("collector_cluster")));
  }
  cluster_ = cluster->info();

  std::string endpoint = config.getString("endpoint");

  tls_.set(
      tls_slot_,
      [this, endpoint](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
        Zipkin::Tracer tracer(local_info_.clusterName(), local_info_.address()->asString());
        tracer.setReporter(
            ZipkinReporter::NewInstance(std::ref(*this), std::ref(dispatcher), endpoint));
        return ThreadLocal::ThreadLocalObjectSharedPtr{
            new TlsZipkinTracer(std::move(tracer), *this)};
      });
}

SpanPtr ZipkinDriver::startSpan(Http::HeaderMap& request_headers, const std::string&,
                                SystemTime start_time) {
  // TODO: start_time is not really used.
  // Need to figure out whether or not it is really needed
  // A new timestamp is currently generated for a new span

  Zipkin::Tracer& tracer = tls_.getTyped<TlsZipkinTracer>(tls_slot_).tracer_;
  ZipkinSpanPtr active_span;
  Zipkin::Span new_zipkin_span;

  if (request_headers.OtSpanContext()) {
    std::cerr << std::endl
              << "Found context" << std::endl;

    // Get the context from the x-b3-envoy header
    // This header contains values of annotations previously set
    // The context built from this header allows the tracer to properly set the span id and the
    // parent id
    // The ids carried in x-b3-envoy also appear in the appropriate B3 headers
    Zipkin::SpanContext context;
    context.populateFromString(request_headers.OtSpanContext()->value().c_str());

    std::cerr << "Context: " << context.serializeToString() << std::endl;

    new_zipkin_span = tracer.startSpan(request_headers.Host()->value().c_str(),
                                       start_time.time_since_epoch().count(), context);
  } else {
    std::cerr << std::endl
              << "No context found. Will create a root span" << std::endl;

    new_zipkin_span = tracer.startSpan(request_headers.Host()->value().c_str(),
                                       start_time.time_since_epoch().count());
  }

  // Set the trace-id and span-id headers properly, based on the newly-created span structure
  request_headers.insertXB3TraceId().value(new_zipkin_span.traceIdAsHexString());
  request_headers.insertXB3SpanId().value(new_zipkin_span.idAsHexString());

  // Set the parent-span header properly, based on the newly-created span structure
  if (new_zipkin_span.isSet().parent_id) {
    request_headers.insertXB3ParentSpanId().value(new_zipkin_span.parentIdAsHexString());
  }

  // Set sampled header
  request_headers.insertXB3Sampled().value(std::string(("1")));

  Zipkin::SpanContext new_span_context(new_zipkin_span);
  std::cerr << "New span context: " << new_span_context.serializeToString() << std::endl;

  // Set the ot-span-context with the new context
  request_headers.insertOtSpanContext().value(new_span_context.serializeToString());

  std::cerr << "ZipkinDriver: span's tracer: " << new_zipkin_span.tracer() << std::endl;

  active_span.reset(new ZipkinSpan(new_zipkin_span));

  return std::move(active_span);
}

ZipkinReporter::ZipkinReporter(ZipkinDriver& driver, Event::Dispatcher& dispatcher,
                               const std::string& endpoint)
    : driver_(driver), endpoint_(endpoint) {
  flush_timer_ = dispatcher.createTimer([this]() -> void {
    flushSpans();
    enableTimer();
  });

  uint64_t min_flush_spans =
      driver_.runtime().snapshot().getInteger("tracing.zipkin.min_flush_spans", 5U);
  span_buffer_.allocateBuffer(min_flush_spans);

  enableTimer();
}

std::unique_ptr<Zipkin::Reporter> ZipkinReporter::NewInstance(ZipkinDriver& driver,
                                                              Event::Dispatcher& dispatcher,
                                                              const std::string& endpoint) {
  return std::unique_ptr<Zipkin::Reporter>(new ZipkinReporter(driver, dispatcher, endpoint));
}

void ZipkinReporter::reportSpan(Zipkin::Span&& span) {
  span_buffer_.addSpan(std::move(span));

  uint64_t min_flush_spans =
      driver_.runtime().snapshot().getInteger("tracing.zipkin.min_flush_spans", 5U);

  std::cerr << "reportSpan() has been called; min_flush: " << min_flush_spans << std::endl;
  std::cerr << "reportSpan() span: " << span.toJson() << std::endl;
  std::cerr << "reportSpan() pending spans: " << span_buffer_.pendingSpans() << std::endl;

  if (span_buffer_.pendingSpans() == min_flush_spans) {
    flushSpans();
  }
}

void ZipkinReporter::enableTimer() {
  uint64_t flush_interval =
      driver_.runtime().snapshot().getInteger("tracing.zipkin.flush_interval_ms", 5000U);
  flush_timer_->enableTimer(std::chrono::milliseconds(flush_interval));
}

void ZipkinReporter::flushSpans() {
  if (span_buffer_.pendingSpans()) {
    std::cerr << "flushSpans() will flush" << std::endl;
    std::string request_body = span_buffer_.toStringifiedJsonArray();
    std::cerr << "HTTP request body" << request_body << std::endl;

    std::cerr << "Will post spans to Zipkin. Endpoint: " << endpoint_ << std::endl;

    Http::MessagePtr message(new Http::RequestMessageImpl());
    message->headers().insertMethod().value(Http::Headers::get().MethodValues.Post);
    message->headers().insertPath().value(endpoint_);
    message->headers().insertHost().value(driver_.cluster()->name());
    message->headers().insertContentType().value(std::string("application/json"));

    Buffer::InstancePtr body(new Buffer::OwnedImpl());
    body->add(request_body);
    message->body() = std::move(body);

    uint64_t timeout =
        driver_.runtime().snapshot().getInteger("tracing.zipkin.request_timeout", 5000U);
    driver_.clusterManager()
        .httpAsyncClientForCluster(driver_.cluster()->name())
        .send(std::move(message), *this, std::chrono::milliseconds(timeout));

    span_buffer_.flush();
  }
}

void ZipkinReporter::onFailure(Http::AsyncClient::FailureReason) {
  std::cerr << "Error posting spans to Zipkin" << std::endl;
}

void ZipkinReporter::onSuccess(Http::MessagePtr&& http_response) {
  if (Http::Utility::getResponseStatus(http_response->headers()) !=
      enumToInt(Http::Code::Accepted)) {
    std::cerr << "Unexpected HTTP response code: "
              << Http::Utility::getResponseStatus(http_response->headers()) << std::endl;
  } else {
    std::cerr << "Successfully posted spans to Zipkin" << std::endl;
  }
}

} // Tracing
