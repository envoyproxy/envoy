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
                                   const SpanContext& span_context,
                                   const SpanContext& previous_context) {
  SpanObject span_object(span_context, previous_context, time_source_, random_generator_);
  span_object.setAsEntrySpan(config.operationName() == Tracing::OperationName::Ingress);
  span_object.setStartTime(getTimestamp(start_time));

  return std::make_unique<Span>(span_object, *this);
}

void Tracer::report(const SpanObject& span_object) { reporter_->report(span_object); }

Tracer::~Tracer() { reporter_->closeStream(); }

Span::Span(SpanObject span_object, Tracer& tracer) : span_object_(span_object), tracer_(tracer) {}

void Span::setOperation(absl::string_view) {}

void Span::setTag(absl::string_view name, absl::string_view value) {
  if (name == Tracing::Tags::get().HttpUrl) {
    span_object_.addTag({UrlTag, std::string(value)});
  }

  if (name == Tracing::Tags::get().HttpStatusCode) {
    span_object_.addTag({StatusCodeTag, std::string(value)});
  }

  if (name == Tracing::Tags::get().Error) {
    span_object_.setAsError(value == Tracing::Tags::get().True);
  }

  if (name == Tracing::Tags::get().PeerAddress && !span_object_.isEntrySpan()) {
    // Set perr when it is an exit span.
    span_object_.setPeer(std::string(value));
  }

  span_object_.addTag({std::string(name), std::string(value)});
}

void Span::log(SystemTime, const std::string&) {}

void Span::finishSpan() {
  span_object_.finish();
  tracer_.report(span_object_);
}

void Span::injectContext(Http::RequestHeaderMap& request_headers) {
  span_object_.context().inject(request_headers);
}

Tracing::SpanPtr Span::spawnChild(const Tracing::Config& config, const std::string&,
                                  SystemTime start_time) {
  return tracer_.startSpan(config, start_time, span_object_.context(),
                           span_object_.previousContext());
}

void Span::setSampled(bool) {}

std::string Span::getBaggage(absl::string_view) { return EMPTY_STRING; }

void Span::setBaggage(absl::string_view, absl::string_view) {}

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
