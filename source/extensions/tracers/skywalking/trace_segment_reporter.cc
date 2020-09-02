#include "extensions/tracers/skywalking/trace_segment_reporter.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

namespace {

SegmentObject toSegmentObject(const SpanObject& span_object) {
  SegmentObject segment_object;
  segment_object.set_traceid(span_object.context().traceId());
  segment_object.set_tracesegmentid(span_object.context().traceSegmentId());
  segment_object.set_service(span_object.context().service());
  segment_object.set_serviceinstance(span_object.context().serviceInstance());

  auto* span = segment_object.mutable_spans()->Add();
  // The SpanLayer is always Http.
  span->set_spanlayer(SpanLayer::Http);
  span->set_spantype(span_object.isEntrySpan() ? SpanType::Entry : SpanType::Exit);

  if (!span_object.peer().empty()) {
    span->set_peer(span_object.peer());
  }

  span->set_componentid(6000);
  span->set_starttime(span_object.startTime());
  span->set_endtime(span_object.endTime());
  span->set_iserror(span_object.isError());
  span->set_operationname(span_object.operationName().empty()
                              ? span_object.context().parentEndpoint()
                              : span_object.operationName());
  span->set_spanid(span_object.spanId());
  span->set_parentspanid(span_object.parentSpanId());

  const auto& previous_context = span_object.previousContext();
  if (!previous_context.isNew()) {
    auto* ref = span->mutable_refs()->Add();
    ref->set_traceid(previous_context.traceId());
    ref->set_parenttracesegmentid(previous_context.traceSegmentId());
    ref->set_parentspanid(previous_context.parentSpanId());
    ref->set_parentservice(previous_context.service());
    ref->set_parentserviceinstance(previous_context.serviceInstance());
    ref->set_parentendpoint(previous_context.parentEndpoint());
    ref->set_networkaddressusedatpeer(previous_context.networkAddressUsedAtPeer());
  }

  for (const auto& span_tag : span_object.tags()) {
    auto* tag = span->mutable_tags()->Add();
    tag->set_key(span_tag.first);
    tag->set_value(span_tag.second);
  }
  return segment_object;
}

} // namespace

TraceSegmentReporter::TraceSegmentReporter(Grpc::AsyncClientFactoryPtr&& factory,
                                           Event::Dispatcher& dispatcher)
    : client_(factory->create()),
      service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "TraceSegmentReportService.collect")) {
  retry_timer_ = dispatcher.createTimer([this]() -> void { establishNewStream(); });
  establishNewStream();
}

void TraceSegmentReporter::report(const SpanObject& span_object) {
  sendTraceSegment(toSegmentObject(span_object));
}

void TraceSegmentReporter::sendTraceSegment(const SegmentObject& request) {
  // TODO(dio): Buffer when stream is not yet established
  if (stream_ != nullptr) {
    stream_->sendMessage(request, false);
  }
}

void TraceSegmentReporter::closeStream() {
  if (stream_ != nullptr) {
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
}

void TraceSegmentReporter::handleFailure() { setRetryTimer(); }

void TraceSegmentReporter::setRetryTimer() {
  retry_timer_->enableTimer(std::chrono::milliseconds(5000));
}

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
