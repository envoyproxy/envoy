#include "extensions/tracers/skywalking/tracer.h"

#include <chrono>

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

constexpr absl::string_view StatusCodeTag = "status_code";
constexpr absl::string_view UrlTag = "url";

namespace {

uint64_t getTimestamp(SystemTime time) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
}

} // namespace

Tracing::SpanPtr Tracer::startSpan(const Tracing::Config&, SystemTime start_time,
                                   const std::string& operation,
                                   SegmentContextSharedPtr segment_context, Span* parent) {
  SpanStore* span_store = segment_context->createSpanStore(parent ? parent->spanStore() : nullptr);

  span_store->setStartTime(getTimestamp(start_time));

  span_store->setOperation(operation);

  return std::make_unique<Span>(std::move(segment_context), span_store, *this);
}

void Span::setOperation(absl::string_view operation) {
  span_store_->setOperation(std::string(operation));
}

void Span::setTag(absl::string_view name, absl::string_view value) {
  if (name == Tracing::Tags::get().HttpUrl) {
    span_store_->addTag(UrlTag, value);
    return;
  }

  if (name == Tracing::Tags::get().HttpStatusCode) {
    span_store_->addTag(StatusCodeTag, value);
    return;
  }

  if (name == Tracing::Tags::get().Error) {
    span_store_->setAsError(value == Tracing::Tags::get().True);
  }

  span_store_->addTag(name, value);
}

// Logs in the SkyWalking format are temporarily unsupported.
void Span::log(SystemTime, const std::string&) {}

void Span::finishSpan() {
  span_store_->setEndTime(DateUtil::nowToMilliseconds(tracer_.time_source_));
  tryToReportSpan();
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
