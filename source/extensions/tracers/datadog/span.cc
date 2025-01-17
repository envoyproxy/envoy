#include "source/extensions/tracers/datadog/span.h"

#include <utility>

#include "source/common/tracing/common_values.h"
#include "source/common/tracing/null_span_impl.h"
#include "source/extensions/tracers/datadog/time_util.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "datadog/dict_writer.h"
#include "datadog/sampling_priority.h"
#include "datadog/span_config.h"
#include "datadog/trace_segment.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {
namespace {

class TraceContextWriter : public datadog::tracing::DictWriter {
public:
  explicit TraceContextWriter(Tracing::TraceContext& context) : context_(context) {}

  void set(datadog::tracing::StringView key, datadog::tracing::StringView value) override {
    context_.set(key, value);
  }

private:
  Tracing::TraceContext& context_;
};

} // namespace

Span::Span(datadog::tracing::Span&& span) : span_(std::move(span)) {}

const datadog::tracing::Optional<datadog::tracing::Span>& Span::impl() const { return span_; }

void Span::setOperation(absl::string_view operation) {
  if (!span_) {
    return;
  }

  // What Envoy calls the operation name more closely corresponds to what
  // Datadog calls the resource name.
  span_->set_resource_name(operation);
}

void Span::setTag(absl::string_view name, absl::string_view value) {
  if (!span_) {
    return;
  }

  const auto& Tags = Envoy::Tracing::Tags::get();

  if (name == "resource.name") {
    // The special "resource.name" tag is a holdover from when the Datadog
    // tracer was OpenTracing-based, and so there was no way to set the Datadog
    // resource name directly.
    // In Envoy, it's still the case that there's no way to set the Datadog
    // resource name directly; so, here if the tag name is "resource.name", we
    // actually set the resource name instead of setting a tag.
    span_->set_resource_name(value);
  } else if (name == Tags.Error) {
    // Envoy marks spans as containing errors by setting the "error" tag.
    // Here we translate into the dd-trace-cpp equivalent.
    if (value == Tags.True) {
      span_->set_error(true);
    }
  } else if (name == Tags.ErrorReason) {
    // Envoy conveys information about an error by setting the "error.reason"
    // tag.
    // Here we translate into the dd-trace-cpp equivalent.
    span_->set_error_message(value);
    span_->set_tag(name, value);
  } else {
    span_->set_tag(name, value);
  }
}

void Span::log(SystemTime, const std::string&) {
  // Datadog spans don't have in-bound "events" or "logs".
}

void Span::finishSpan() { span_.reset(); }

void Span::injectContext(Tracing::TraceContext& trace_context, const Tracing::UpstreamContext&) {
  if (!span_) {
    return;
  }

  TraceContextWriter writer{trace_context};
  span_->inject(writer);
}

Tracing::SpanPtr Span::spawnChild(const Tracing::Config&, const std::string& name,
                                  SystemTime start_time) {
  if (!span_) {
    // I don't expect this to happen. This means that `spawnChild` was called
    // after `finishSpan`.
    return std::make_unique<Tracing::NullSpan>();
  }

  // The OpenTracing implementation ignored the `Tracing::Config` argument,
  // so we will as well.
  // The `name` parameter to this function more closely matches Datadog's
  // concept of "resource name." Datadog's "span name," or "operation name,"
  // instead describes the category of operation being performed, which here
  // we hard-code.
  datadog::tracing::SpanConfig config;
  config.name = "envoy.proxy";
  config.resource = name;
  config.start = estimateTime(start_time);

  return std::make_unique<Span>(span_->create_child(config));
}

void Span::setSampled(bool sampled) {
  if (!span_) {
    return;
  }

  auto priority = static_cast<int>(sampled ? datadog::tracing::SamplingPriority::USER_KEEP
                                           : datadog::tracing::SamplingPriority::USER_DROP);
  span_->trace_segment().override_sampling_priority(priority);
}

std::string Span::getBaggage(absl::string_view) {
  // not implemented
  return EMPTY_STRING;
}

void Span::setBaggage(absl::string_view, absl::string_view) {
  // not implemented
}

std::string Span::getTraceId() const {
  if (!span_) {
    return EMPTY_STRING;
  }
  return absl::StrCat(absl::Hex(span_->id()));
}

std::string Span::getSpanId() const {
  // TODO(#34412): This method is not yet implemented for Datadog.
  return EMPTY_STRING;
}

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
