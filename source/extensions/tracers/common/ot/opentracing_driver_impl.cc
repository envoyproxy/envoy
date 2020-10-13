#include "extensions/tracers/common/ot/opentracing_driver_impl.h"

#include <sstream>

#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/common/base64.h"
#include "common/common/utility.h"
#include "common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Common {
namespace Ot {

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    ot_span_context_handle(Http::CustomHeaders::get().OtSpanContext);

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
    request_headers_.addCopy(std::move(lowercase_key), {value.data(), value.size()});
    return {};
  }

private:
  Http::HeaderMap& request_headers_;
};

class OpenTracingHTTPHeadersReader : public opentracing::HTTPHeadersReader {
public:
  explicit OpenTracingHTTPHeadersReader(const Http::RequestHeaderMap& request_headers)
      : request_headers_(request_headers) {}

  using OpenTracingCb = std::function<opentracing::expected<void>(opentracing::string_view,
                                                                  opentracing::string_view)>;

  // opentracing::HTTPHeadersReader
  opentracing::expected<opentracing::string_view>
  LookupKey(opentracing::string_view key) const override {
    const auto entry = request_headers_.get(Http::LowerCaseString{key});
    if (!entry.empty()) {
      // This is an implicitly untrusted header, so only the first value is used.
      return opentracing::string_view{entry[0]->value().getStringView().data(),
                                      entry[0]->value().getStringView().length()};
    } else {
      return opentracing::make_unexpected(opentracing::key_not_found_error);
    }
  }

  opentracing::expected<void> ForeachKey(OpenTracingCb f) const override {
    request_headers_.iterate(headerMapCallback(f));
    return {};
  }

private:
  const Http::RequestHeaderMap& request_headers_;

  static Http::HeaderMap::ConstIterateCb headerMapCallback(OpenTracingCb callback) {
    return [callback =
                std::move(callback)](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
      opentracing::string_view key{header.key().getStringView().data(),
                                   header.key().getStringView().length()};
      opentracing::string_view value{header.value().getStringView().data(),
                                     header.value().getStringView().length()};
      if (callback(key, value)) {
        return Http::HeaderMap::Iterate::Continue;
      } else {
        return Http::HeaderMap::Iterate::Break;
      }
    };
  }
};
} // namespace

OpenTracingSpan::OpenTracingSpan(OpenTracingDriver& driver,
                                 std::unique_ptr<opentracing::Span>&& span)
    : driver_{driver}, span_(std::move(span)) {}

void OpenTracingSpan::finishSpan() { span_->FinishWithOptions(finish_options_); }

void OpenTracingSpan::setOperation(absl::string_view operation) {
  span_->SetOperationName({operation.data(), operation.length()});
}

void OpenTracingSpan::setTag(absl::string_view name, absl::string_view value) {
  span_->SetTag({name.data(), name.length()},
                opentracing::v2::string_view{value.data(), value.length()});
}

void OpenTracingSpan::log(SystemTime timestamp, const std::string& event) {
  opentracing::LogRecord record{timestamp, {{Tracing::Logs::get().EventKey, event}}};
  finish_options_.log_records.emplace_back(std::move(record));
}

void OpenTracingSpan::setBaggage(absl::string_view key, absl::string_view value) {
  span_->SetBaggageItem({key.data(), key.length()}, {value.data(), value.length()});
}

std::string OpenTracingSpan::getBaggage(absl::string_view key) {
  return span_->BaggageItem({key.data(), key.length()});
}

void OpenTracingSpan::injectContext(Http::RequestHeaderMap& request_headers) {
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
    request_headers.setInline(
        ot_span_context_handle.handle(),
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

OpenTracingDriver::OpenTracingDriver(Stats::Scope& scope)
    : tracer_stats_{OPENTRACING_TRACER_STATS(POOL_COUNTER_PREFIX(scope, "tracing.opentracing."))} {}

Tracing::SpanPtr OpenTracingDriver::startSpan(const Tracing::Config& config,
                                              Http::RequestHeaderMap& request_headers,
                                              const std::string& operation_name,
                                              SystemTime start_time,
                                              const Tracing::Decision tracing_decision) {
  const PropagationMode propagation_mode = this->propagationMode();
  const opentracing::Tracer& tracer = this->tracer();
  std::unique_ptr<opentracing::Span> active_span;
  std::unique_ptr<opentracing::SpanContext> parent_span_ctx;
  if (propagation_mode == PropagationMode::SingleHeader &&
      request_headers.getInline(ot_span_context_handle.handle())) {
    opentracing::expected<std::unique_ptr<opentracing::SpanContext>> parent_span_ctx_maybe;
    std::string parent_context = Base64::decode(
        std::string(request_headers.getInlineValue(ot_span_context_handle.handle())));

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
