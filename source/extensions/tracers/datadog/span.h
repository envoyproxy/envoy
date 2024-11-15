#pragma once

#include <optional>

#include "envoy/common/time.h"
#include "envoy/tracing/trace_driver.h"

#include "datadog/span.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

/**
 * Tracing::Span implementation for use in Datadog tracing. This class contains
 * an optional<datadog::tracing::Span> and forwards its member functions to the
 * corresponding member functions of datadog::tracing::Span.
 *
 * datadog::tracing::Span is the span type used in Datadog's core tracing
 * library, dd-trace-cpp. It's wrapped in an optional<> because the lifetime
 * of a datadog::tracing::Span is tied to the scope of the object itself,
 * whereas the Tracing::Span implemented here has a finishSpan() member
 * function that allows the span's lifetime to end without destroying the
 * object.
 *
 * For the same reason, this class has two states: one when the
 * optional<datadog::tracing::Span> is not empty and member functions are
 * forwarded to it, and another state when the optional<datadog::tracing::Span>
 * is empty and member functions have no effect.
 */
class Span : public Tracing::Span {
public:
  explicit Span(datadog::tracing::Span&& span);

  const datadog::tracing::Optional<datadog::tracing::Span>& impl() const;

  // Envoy::Tracing::Span
  void setOperation(absl::string_view operation) override;
  void setTag(absl::string_view name, absl::string_view value) override;
  void log(SystemTime, const std::string&) override;
  void finishSpan() override;
  void injectContext(Tracing::TraceContext& trace_context,
                     const Tracing::UpstreamContext& upstream) override;
  Tracing::SpanPtr spawnChild(const Tracing::Config& config, const std::string& name,
                              SystemTime start_time) override;
  void setSampled(bool sampled) override;
  std::string getBaggage(absl::string_view key) override;
  void setBaggage(absl::string_view key, absl::string_view value) override;
  std::string getTraceId() const override;
  std::string getSpanId() const override;

private:
  datadog::tracing::Optional<datadog::tracing::Span> span_;
};

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
