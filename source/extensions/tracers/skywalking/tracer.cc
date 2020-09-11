#include "extensions/tracers/skywalking/tracer.h"

#include <chrono>

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

constexpr char StatusCodeTag[] = "status_code";
constexpr char UrlTag[] = "url";

namespace {

uint64_t getTimestamp(SystemTime time) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
}

} // namespace

Tracing::SpanPtr Tracer::startSpan(const Tracing::Config& config, SystemTime start_time,
                                   const std::string& operation_name,
                                   SegmentContextSharedPtr segment_context, Span* parent_span) {
  SpanStore* span_store = segment_context->createSpanStore(
      time_source_, parent_span ? parent_span->spanStore() : nullptr);
  span_store->setAsEntrySpan(config.operationName() == Tracing::OperationName::Ingress);
  span_store->setStartTime(getTimestamp(start_time));

  // TODO(wbpcode): I am not sure if the operation name and endpoint need to be consistent. So here
  // may need to be improved.
  span_store->setOperation(operation_name);

  return std::make_unique<Span>(std::move(segment_context), span_store, *this);
}

void Span::setOperation(absl::string_view operation) {
  span_store_->setOperation(std::string(operation));
}

void Span::setTag(absl::string_view name, absl::string_view value) {
  if (name == Tracing::Tags::get().HttpUrl) {
    span_store_->addTag(UrlTag, value);
  }

  if (name == Tracing::Tags::get().HttpStatusCode) {
    span_store_->addTag(StatusCodeTag, value);
  }

  if (name == Tracing::Tags::get().Error) {
    span_store_->setAsError(value == Tracing::Tags::get().True);
  }

  if (name == Tracing::Tags::get().PeerAddress && !span_store_->isEntrySpan()) {
    // Set peer when it is an exit span.
    span_store_->setPeer(std::string(value));
  }

  span_store_->addTag(name, value);
}

void Span::log(SystemTime, const std::string&) {}

void Span::finishSpan() {
  span_store_->finish();
  // If the current span is the first span of the entire segment and its sampling flag is not false,
  // the data for the entire segment is reported.
  if (span_store_->sampled() && span_store_->spanId() == 0) {
    tracer_.report(*segment_context_);
  }
}

void Span::injectContext(Http::RequestHeaderMap& request_headers) {
  span_store_->injectContext(request_headers);
}

Tracing::SpanPtr Span::spawnChild(const Tracing::Config& config, const std::string& operation_name,
                                  SystemTime start_time) {
  // The new child span will share the same context with the parent span.
  return tracer_.startSpan(config, start_time, operation_name, segment_context_, this);
}

void Span::setSampled(bool sampled) { span_store_->setSampled(sampled ? 1 : 0); }

std::string Span::getBaggage(absl::string_view) { return EMPTY_STRING; }

void Span::setBaggage(absl::string_view, absl::string_view) {}

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
