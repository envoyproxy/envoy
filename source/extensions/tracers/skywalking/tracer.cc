#include "source/extensions/tracers/skywalking/tracer.h"

#include <string>

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

namespace {
static constexpr absl::string_view StatusCodeTag = "status_code";
static constexpr absl::string_view UrlTag = "url";
} // namespace

const Http::LowerCaseString& skywalkingPropagationHeaderKey() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, "sw8");
}

void Span::setTag(absl::string_view name, absl::string_view value) {
  if (name == Tracing::Tags::get().HttpUrl) {
    span_entity_->addTag(UrlTag.data(), std::string(value));
  } else if (name == Tracing::Tags::get().HttpStatusCode) {
    span_entity_->addTag(StatusCodeTag.data(), std::string(value));
  } else if (name == Tracing::Tags::get().Error) {
    span_entity_->setErrorStatus();
    span_entity_->addTag(std::string(name), std::string(value));
  } else {
    span_entity_->addTag(std::string(name), std::string(value));
  }
}

void Span::setSampled(bool do_sample) {
  // Sampling status is always true on SkyWalking. But with disabling skip_analysis,
  // this span can't be analyzed.
  if (!do_sample) {
    span_entity_->setSkipAnalysis();
  }
}

void Span::log(SystemTime, const std::string& event) { span_entity_->addLog(EMPTY_STRING, event); }

void Span::finishSpan() {
  span_entity_->endSpan();
  parent_tracer_.sendSegment(tracing_context_);
}

void Span::injectContext(Tracing::TraceContext& trace_context) {
  // TODO(wbpcode): Due to https://github.com/SkyAPM/cpp2sky/issues/83 in cpp2sky, it is necessary
  // to ensure that there is '\0' at the end of the string_view parameter to ensure that the
  // corresponding trace header is generated correctly. For this reason, we cannot directly use host
  // as argument. We need create a copy of std::string based on host and std::string will
  // automatically add '\0' to the end of the string content.
  auto sw8_header = tracing_context_->createSW8HeaderValue(std::string(trace_context.authority()));
  if (sw8_header.has_value()) {
    trace_context.setByReferenceKey(skywalkingPropagationHeaderKey(), sw8_header.value());
  }
}

Tracing::SpanPtr Span::spawnChild(const Tracing::Config&, const std::string& name, SystemTime) {
  return std::make_unique<Span>(name, *this, tracing_context_, parent_tracer_);
}

Tracer::Tracer(TraceSegmentReporterPtr reporter) : reporter_(std::move(reporter)) {}

void Tracer::sendSegment(TracingContextPtr segment_context) {
  ASSERT(reporter_);
  if (segment_context->readyToSend()) {
    reporter_->report(std::move(segment_context));
  }
}

Tracing::SpanPtr Tracer::startSpan(const std::string& name, TracingContextPtr tracing_context) {
  return std::make_unique<Span>(name, tracing_context, *this);
}
} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
