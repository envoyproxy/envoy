#include "extensions/tracers/skywalking/trace_segment_reporter.h"

#include "envoy/http/header_map.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

namespace {

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    authentication_handle(Http::CustomHeaders::get().Authentication);

// Convert SegmentContext to SegmentObject.
TraceSegmentPtr toSegmentObject(const SegmentContext& segment_context) {
  auto new_segment_ptr = std::make_unique<SegmentObject>();
  SegmentObject& segment_object = *new_segment_ptr;

  segment_object.set_traceid(segment_context.traceId());
  segment_object.set_tracesegmentid(segment_context.traceSegmentId());
  segment_object.set_service(segment_context.service());
  segment_object.set_serviceinstance(segment_context.serviceInstance());

  for (const auto& span_store : segment_context.spanList()) {
    if (!span_store->sampled()) {
      continue;
    }
    auto* span = segment_object.mutable_spans()->Add();

    span->set_spanlayer(SpanLayer::Http);
    span->set_spantype(span_store->isEntrySpan() ? SpanType::Entry : SpanType::Exit);
    // Please check
    // https://github.com/apache/skywalking/blob/master/oap-server/server-bootstrap/src/main/resources/component-libraries.yml
    // get more information.
    span->set_componentid(9000);

    if (!span_store->peerAddress().empty() && span_store->isEntrySpan()) {
      span->set_peer(span_store->peerAddress());
    }

    span->set_spanid(span_store->spanId());
    span->set_parentspanid(span_store->parentSpanId());

    span->set_starttime(span_store->startTime());
    span->set_endtime(span_store->endTime());

    span->set_iserror(span_store->isError());

    span->set_operationname(span_store->operation());

    auto& tags = *span->mutable_tags();
    tags.Reserve(span_store->tags().size());

    for (auto& span_tag : span_store->tags()) {
      KeyStringValuePair* new_tag = tags.Add();
      new_tag->set_key(span_tag.first);
      new_tag->set_value(span_tag.second);
    }

    SpanContext* previous_span_context = segment_context.previousSpanContext();

    if (!previous_span_context || !span_store->isEntrySpan()) {
      continue;
    }

    auto* ref = span->mutable_refs()->Add();
    ref->set_traceid(previous_span_context->trace_id_);
    ref->set_parenttracesegmentid(previous_span_context->trace_segment_id_);
    ref->set_parentspanid(previous_span_context->span_id_);
    ref->set_parentservice(previous_span_context->service_);
    ref->set_parentserviceinstance(previous_span_context->service_instance_);
    ref->set_parentendpoint(previous_span_context->endpoint_);
    ref->set_networkaddressusedatpeer(previous_span_context->target_address_);
  }
  return new_segment_ptr;
}

} // namespace

TraceSegmentReporter::TraceSegmentReporter(Grpc::AsyncClientFactoryPtr&& factory,
                                           Event::Dispatcher& dispatcher,
                                           Random::RandomGenerator& random_generator,
                                           SkyWalkingTracerStats& stats,
                                           const SkyWalkingClientConfig& client_config)
    : tracing_stats_(stats), client_config_(client_config), client_(factory->create()),
      service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "TraceSegmentReportService.collect")),
      random_generator_(random_generator) {

  static constexpr uint32_t RetryInitialDelayMs = 500;
  static constexpr uint32_t RetryMaxDelayMs = 30000;
  backoff_strategy_ = std::make_unique<JitteredExponentialBackOffStrategy>(
      RetryInitialDelayMs, RetryMaxDelayMs, random_generator_);

  retry_timer_ = dispatcher.createTimer([this]() -> void { establishNewStream(); });
  establishNewStream();
}

void TraceSegmentReporter::onCreateInitialMetadata(Http::RequestHeaderMap& metadata) {
  if (!client_config_.backendToken().empty()) {
    metadata.setInline(authentication_handle.handle(), client_config_.backendToken());
  }
}

void TraceSegmentReporter::report(const SegmentContext& segment_context) {
  sendTraceSegment(toSegmentObject(segment_context));
}

void TraceSegmentReporter::sendTraceSegment(TraceSegmentPtr request) {
  ASSERT(request);
  ENVOY_LOG(trace, "Try to report segment to SkyWalking Server:\n{}", request->DebugString());

  if (stream_ != nullptr) {
    tracing_stats_.segments_sent_.inc();
    stream_->sendMessage(*request, false);
    return;
  }
  // Null stream_ and cache segment data temporarily.
  delayed_segments_cache_.emplace(std::move(request));
  if (delayed_segments_cache_.size() > client_config_.maxCacheSize()) {
    tracing_stats_.segments_dropped_.inc();
    delayed_segments_cache_.pop();
  }
}

void TraceSegmentReporter::flushTraceSegments() {
  ENVOY_LOG(debug, "Flush segments in cache to SkyWalking backend service");
  while (!delayed_segments_cache_.empty() && stream_ != nullptr) {
    tracing_stats_.segments_sent_.inc();
    tracing_stats_.segments_flushed_.inc();
    stream_->sendMessage(*delayed_segments_cache_.front(), false);
    delayed_segments_cache_.pop();
  }
  tracing_stats_.cache_flushed_.inc();
}

void TraceSegmentReporter::closeStream() {
  if (stream_ != nullptr) {
    flushTraceSegments();
    stream_->closeStream();
  }
}

void TraceSegmentReporter::onRemoteClose(Grpc::Status::GrpcStatus status,
                                         const std::string& message) {
  ENVOY_LOG(debug, "{} gRPC stream closed: {}, {}", service_method_.name(), status, message);
  stream_ = nullptr;
  handleFailure();
}

void TraceSegmentReporter::establishNewStream() {
  ENVOY_LOG(debug, "Try to create new {} gRPC stream for reporter", service_method_.name());
  stream_ = client_->start(service_method_, *this, Http::AsyncClient::StreamOptions());
  if (stream_ == nullptr) {
    ENVOY_LOG(debug, "Failed to create {} gRPC stream", service_method_.name());
    return;
  }
  // TODO(wbpcode): Even if stream_ is not empty, there is no guarantee that the connection will be
  // established correctly. If there is a connection failure, the onRemoteClose method will be
  // called. Currently, we lack a way to determine whether the connection is truly available. This
  // may cause partial data loss.
  if (!delayed_segments_cache_.empty()) {
    flushTraceSegments();
  }
  backoff_strategy_->reset();
}

void TraceSegmentReporter::handleFailure() { setRetryTimer(); }

void TraceSegmentReporter::setRetryTimer() {
  retry_timer_->enableTimer(std::chrono::milliseconds(backoff_strategy_->nextBackOffMs()));
}

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
