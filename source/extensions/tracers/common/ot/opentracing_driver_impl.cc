#include "extensions/tracers/common/ot/opentracing_driver_impl.h"

#include <sstream>

#include "common/common/assert.h"
#include "common/common/base64.h"
#include "common/common/utility.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Common {
namespace Ot {

namespace {
class OpenTracingHTTPHeadersWriter : public opentracing::HTTPHeadersWriter {
public:
  explicit OpenTracingHTTPHeadersWriter(Http::HeaderMap& request_headers)
      : request_headers_(request_headers) {}

  // opentracing::HTTPHeadersWriter
  opentracing::expected<void> Set(opentracing::string_view key,
                                  opentracing::string_view value) const override {
    Http::LowerCaseString lowercase_key{key};
    request_headers_.remove(lowercase_key);
    request_headers_.addCopy(std::move(lowercase_key), value);
    return {};
  }

private:
  Http::HeaderMap& request_headers_;
};

class OpenTracingHTTPHeadersReader : public opentracing::HTTPHeadersReader {
public:
  explicit OpenTracingHTTPHeadersReader(const Http::HeaderMap& request_headers)
      : request_headers_(request_headers) {}

  typedef std::function<opentracing::expected<void>(opentracing::string_view,
                                                    opentracing::string_view)>
      OpenTracingCb;

  // opentracing::HTTPHeadersReader
  opentracing::expected<opentracing::string_view>
  LookupKey(opentracing::string_view key) const override {
    const Http::HeaderEntry* entry;
    Http::HeaderMap::Lookup lookup_result =
        request_headers_.lookup(Http::LowerCaseString{key}, &entry);
    switch (lookup_result) {
    case Http::HeaderMap::Lookup::Found:
      return opentracing::string_view{entry->value().c_str(), entry->value().size()};
    case Http::HeaderMap::Lookup::NotFound:
      return opentracing::make_unexpected(opentracing::key_not_found_error);
    case Http::HeaderMap::Lookup::NotSupported:
      return opentracing::make_unexpected(opentracing::lookup_key_not_supported_error);
    }
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  opentracing::expected<void> ForeachKey(OpenTracingCb f) const override {
    request_headers_.iterate(headerMapCallback, static_cast<void*>(&f));
    return {};
  }

private:
  const Http::HeaderMap& request_headers_;

  static Http::HeaderMap::Iterate headerMapCallback(const Http::HeaderEntry& header,
                                                    void* context) {
    OpenTracingCb* callback = static_cast<OpenTracingCb*>(context);
    opentracing::string_view key{header.key().c_str(), header.key().size()};
    opentracing::string_view value{header.value().c_str(), header.value().size()};
    if ((*callback)(key, value)) {
      return Http::HeaderMap::Iterate::Continue;
    } else {
      return Http::HeaderMap::Iterate::Break;
    }
  }
};
} // namespace

OpenTracingSpan::OpenTracingSpan(OpenTracingDriver& driver,
                                 std::unique_ptr<opentracing::Span>&& span)
    : driver_{driver}, span_(std::move(span)) {}

void OpenTracingSpan::finishSpan() { span_->Finish(); }

void OpenTracingSpan::setOperation(const std::string& operation) {
  span_->SetOperationName(operation);
}

void OpenTracingSpan::setTag(const std::string& name, const std::string& value) {
  span_->SetTag(name, value);
}

void OpenTracingSpan::injectContext(Http::HeaderMap& request_headers) {
  if (driver_.propagationMode() == OpenTracingDriver::PropagationMode::SingleHeader) {
    // Inject the span context using Envoy's single-header format.
    std::ostringstream oss;
    const opentracing::expected<void> was_successful =
        span_->tracer().Inject(span_->context(), oss);
    if (!was_successful) {
      ENVOY_LOG(debug, "Failed to inject span context: {}", was_successful.error().message());
      driver_.tracerStats().span_context_injection_error_.inc();
      return;
    }
    const std::string current_span_context = oss.str();
    request_headers.insertOtSpanContext().value(
        Base64::encode(current_span_context.c_str(), current_span_context.length()));
  } else {
    // Inject the context using the tracer's standard HTTP header format.
    const OpenTracingHTTPHeadersWriter writer{request_headers};
    const opentracing::expected<void> was_successful =
        span_->tracer().Inject(span_->context(), writer);
    if (!was_successful) {
      ENVOY_LOG(debug, "Failed to inject span context: {}", was_successful.error().message());
      driver_.tracerStats().span_context_injection_error_.inc();
      return;
    }
  }
}

void OpenTracingSpan::setSampled(bool sampled) {
  span_->SetTag(opentracing::ext::sampling_priority, sampled ? 1 : 0);
}

Tracing::SpanPtr OpenTracingSpan::spawnChild(const Tracing::Config&, const std::string& name,
                                             SystemTime start_time) {
  std::unique_ptr<opentracing::Span> ot_span = span_->tracer().StartSpan(
      name, {opentracing::ChildOf(&span_->context()), opentracing::StartTimestamp(start_time)});
  RELEASE_ASSERT(ot_span != nullptr, "");
  return Tracing::SpanPtr{new OpenTracingSpan{driver_, std::move(ot_span)}};
}

OpenTracingDriver::OpenTracingDriver(Stats::Store& stats)
    : tracer_stats_{OPENTRACING_TRACER_STATS(POOL_COUNTER_PREFIX(stats, "tracing.opentracing."))} {}

Tracing::SpanPtr OpenTracingDriver::startSpan(const Tracing::Config& config,
                                              Http::HeaderMap& request_headers,
                                              const std::string& operation_name,
                                              SystemTime start_time,
                                              const Tracing::Decision tracing_decision) {
  const PropagationMode propagation_mode = this->propagationMode();
  const opentracing::Tracer& tracer = this->tracer();
  std::unique_ptr<opentracing::Span> active_span;
  std::unique_ptr<opentracing::SpanContext> parent_span_ctx;
  if (propagation_mode == PropagationMode::SingleHeader && request_headers.OtSpanContext()) {
    opentracing::expected<std::unique_ptr<opentracing::SpanContext>> parent_span_ctx_maybe;
    std::string parent_context = Base64::decode(request_headers.OtSpanContext()->value().c_str());

    if (!parent_context.empty()) {
      InputConstMemoryStream istream{parent_context.data(), parent_context.size()};
      parent_span_ctx_maybe = tracer.Extract(istream);
    } else {
      parent_span_ctx_maybe =
          opentracing::make_unexpected(opentracing::span_context_corrupted_error);
    }

    if (parent_span_ctx_maybe) {
      parent_span_ctx = std::move(*parent_span_ctx_maybe);
    } else {
      ENVOY_LOG(debug, "Failed to extract span context: {}",
                parent_span_ctx_maybe.error().message());
      tracerStats().span_context_extraction_error_.inc();
    }
  } else if (propagation_mode == PropagationMode::TracerNative) {
    const OpenTracingHTTPHeadersReader reader{request_headers};
    opentracing::expected<std::unique_ptr<opentracing::SpanContext>> parent_span_ctx_maybe =
        tracer.Extract(reader);
    if (parent_span_ctx_maybe) {
      parent_span_ctx = std::move(*parent_span_ctx_maybe);
    } else {
      ENVOY_LOG(debug, "Failed to extract span context: {}",
                parent_span_ctx_maybe.error().message());
      tracerStats().span_context_extraction_error_.inc();
    }
  }
  opentracing::StartSpanOptions options;
  options.references.emplace_back(opentracing::SpanReferenceType::ChildOfRef,
                                  parent_span_ctx.get());
  options.start_system_timestamp = start_time;
  if (!tracing_decision.traced) {
    options.tags.emplace_back(opentracing::ext::sampling_priority, 0);
  }
  active_span = tracer.StartSpanWithOptions(operation_name, options);
  RELEASE_ASSERT(active_span != nullptr, "");
  active_span->SetTag(opentracing::ext::span_kind,
                      config.operationName() == Tracing::OperationName::Egress
                          ? opentracing::ext::span_kind_rpc_client
                          : opentracing::ext::span_kind_rpc_server);
  return Tracing::SpanPtr{new OpenTracingSpan{*this, std::move(active_span)}};
}

} // namespace Ot
} // namespace Common
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
