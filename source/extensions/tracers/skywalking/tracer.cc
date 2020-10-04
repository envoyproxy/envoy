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

// A simple tool function. Used to extract the HTTP Host from the URL, and set the Host value as the
// peer address to the SpanStore object.
void setSkyWalkingPeerAddress(SpanStore* span_store, absl::string_view http_request_url) {
  if (span_store->isEntrySpan()) {
    return;
  }
  if (size_t host_start_pos = http_request_url.find("://"); host_start_pos != std::string::npos) {
    absl::string_view host_with_path = http_request_url.substr(host_start_pos + 3);
    size_t path_start_pos = host_with_path.find("/");
    if (path_start_pos == std::string::npos) {
      return;
    }
    host_with_path.remove_suffix(host_with_path.size() - path_start_pos);
    span_store->setPeerAddress(std::string(host_with_path));
  }
}

} // namespace

Tracing::SpanPtr Tracer::startSpan(const Tracing::Config& config, SystemTime start_time,
                                   const std::string& operation,
                                   SegmentContextSharedPtr segment_context, Span* parent) {
  SpanStore* span_store =
      segment_context->createSpanStore(time_source_, parent ? parent->spanStore() : nullptr);
  span_store->setAsEntrySpan(config.operationName() == Tracing::OperationName::Ingress);
  span_store->setStartTime(getTimestamp(start_time));

  span_store->setOperation(operation);

  return std::make_unique<Span>(std::move(segment_context), span_store, *this);
}

void Span::setOperation(absl::string_view operation) {
  span_store_->setOperation(std::string(operation));
}

void Span::setTag(absl::string_view name, absl::string_view value) {
  // Use request host as peer address and set it in ExitSpan. The request host can be parsed from
  // the URL.
  if (name == Tracing::Tags::get().HttpUrl) {
    span_store_->addTag(UrlTag, value);
    setSkyWalkingPeerAddress(span_store_, value);
    return;
  }

  if (name == Tracing::Tags::get().HttpStatusCode) {
    span_store_->addTag(StatusCodeTag, value);
    return;
  }

  if (name == Tracing::Tags::get().Error) {
    span_store_->setAsError(value == Tracing::Tags::get().True);
  }

  // When we need to create a new propagation header and send it to upstream, use upstream address
  // as the target address.
  if (name == Tracing::Tags::get().UpstreamAddress) {
    span_store_->setUpstreamAddress(std::string(value));
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
