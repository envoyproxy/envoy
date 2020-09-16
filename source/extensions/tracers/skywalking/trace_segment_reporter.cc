#include "extensions/tracers/skywalking/trace_segment_reporter.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

namespace {
static constexpr uint32_t DEFAULT_DELAYED_SEGMENTS_CACHE_SIZE = 1024;

// Convert SegmentContext to SegmentObject.
TraceSegmentPtr toSegmentObject(const SegmentContext& segment_context) {
  auto new_segment = std::make_unique<SegmentObject>();
  SegmentObject& segment_object = *new_segment;

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
    span->set_componentid(6000);

    if (!span_store->peer().empty()) {
      span->set_peer(span_store->peer());
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
      tags.Add(std::move(const_cast<Tag&>(span_tag)));
    }

    SpanContext* previous_span_context = segment_context.previousSpanContext();
    if (!previous_span_context) {
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
  return new_segment;
}

const Http::LowerCaseString& authenticationTokenKey() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, "Authentication");
}

} // namespace

TraceSegmentReporter::TraceSegmentReporter(
    Grpc::AsyncClientFactoryPtr&& factory, Event::Dispatcher& dispatcher,
    SkyWalkingTracerStats& stats, const envoy::config::trace::v3::ClientConfig& client_config)
    : tracing_stats_(stats), simple_authentication_token_(client_config.authentication()),
      client_(factory->create()),
      service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "TraceSegmentReportService.collect")) {
  max_delayed_segments_cache_size_ = client_config.max_cache_size() == 0
                                         ? DEFAULT_DELAYED_SEGMENTS_CACHE_SIZE
                                         : client_config.max_cache_size();

  retry_timer_ = dispatcher.createTimer([this]() -> void { establishNewStream(); });
  establishNewStream();
}

void TraceSegmentReporter::onCreateInitialMetadata(Http::RequestHeaderMap& metadata) {
  if (!simple_authentication_token_.empty()) {
    metadata.setReferenceKey(authenticationTokenKey(), simple_authentication_token_);
  }
}

void TraceSegmentReporter::report(const SegmentContext& segment_context) {
  sendTraceSegment(toSegmentObject(segment_context));
}

void TraceSegmentReporter::sendTraceSegment(TraceSegmentPtr&& request) {
  if (stream_ != nullptr) {
    tracing_stats_.segments_sent_.inc();
    stream_->sendMessage(*request, false);
    return;
  }
  // Null stream_ and cache segment data temporarily.
  delayed_segments_cache_.emplace(std::move(request));
  if (delayed_segments_cache_.size() > max_delayed_segments_cache_size_) {
    tracing_stats_.segments_dropped_.inc();
    delayed_segments_cache_.pop();
  }
}

void TraceSegmentReporter::flushTraceSegments() {
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

void TraceSegmentReporter::onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) {
  stream_ = nullptr;
  handleFailure();
}

void TraceSegmentReporter::establishNewStream() {
  stream_ = client_->start(service_method_, *this, Http::AsyncClient::StreamOptions());
  if (stream_ == nullptr) {
    handleFailure();
    return;
  }
  if (!delayed_segments_cache_.empty()) {
    flushTraceSegments();
  }
}

void TraceSegmentReporter::handleFailure() { setRetryTimer(); }

void TraceSegmentReporter::setRetryTimer() {
  retry_timer_->enableTimer(std::chrono::milliseconds(5000));
}

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
