#include "extensions/tracers/skywalking/tracer.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

namespace {
static constexpr absl::string_view StatusCodeTag = "status_code";
static constexpr absl::string_view UrlTag = "url";
} // namespace

Tracer::Tracer(TraceSegmentReporterPtr reporter) : reporter_(std::move(reporter)) {}

void Tracer::sendSegment(SegmentContextPtr segment_context) {
  ASSERT(reporter_);
  reporter_->report(std::move(segment_context));
}

Tracing::SpanPtr Tracer::startSpan(const Tracing::Config&, SystemTime, const std::string& operation,
                                   SegmentContextPtr segment_context,
                                   CurrentSegmentSpanPtr parent) {
  Tracing::SpanPtr span;
  auto span_entity = parent != nullptr ? segment_context->createCurrentSegmentSpan(parent)
                                       : segment_context->createCurrentSegmentRootSpan();
  span_entity->startSpan();
  span = std::make_unique<Span>(span_entity, segment_context);
  span->setOperation(operation);
  return span;
}

void Span::setOperation(absl::string_view operation) {
  span_entity_->setOperationName(operation.data());
}

void Span::setTag(absl::string_view name, absl::string_view value) {
  if (name == Tracing::Tags::get().HttpUrl) {
    span_entity_->addTag(UrlTag.data(), value.data());
  } else if (name == Tracing::Tags::get().HttpStatusCode) {
    span_entity_->addTag(StatusCodeTag.data(), value.data());
  } else if (name == Tracing::Tags::get().Error) {
    span_entity_->errorOccured();
    span_entity_->addTag(name.data(), value.data());
  } else {
    span_entity_->addTag(name.data(), value.data());
  }
}

void Span::log(SystemTime, const std::string& event) { span_entity_->addLog(EMPTY_STRING, event); }

void Span::finishSpan() {
  span_entity_->endSpan();
  // TODO(shikugawa): implement reporting trigger.
}

void Span::injectContext(Http::RequestHeaderMap& request_headers) {
  request_headers.setReferenceKey(
      kSkywalkingPropagationHeaderKey,
      segment_context_->createSW8HeaderValue(request_headers.getHostValue().data()));
}

Tracing::SpanPtr Span::spawnChild(const Tracing::Config&, const std::string& name, SystemTime) {
  auto child_span = segment_context_->createCurrentSegmentSpan(span_entity_);
  child_span->startSpan();
  child_span->setOperationName(name);
  return std::make_unique<Span>(child_span, segment_context_);
}

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
