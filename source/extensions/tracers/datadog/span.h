#pragma once

#include <datadog/span.h>

#include <optional>

#include "envoy/common/time.h"
#include "envoy/tracing/trace_driver.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

class Span : public Tracing::Span {
  datadog::tracing::Optional<datadog::tracing::Span> span_;
  std::string trace_id_hex_;

public:
  explicit Span(datadog::tracing::Span&& span);

  // `impl` is used by the unit tests.
  const datadog::tracing::Optional<datadog::tracing::Span>& impl() const;

  void setOperation(absl::string_view operation) override;

  void setTag(absl::string_view name, absl::string_view value) override;

  void log(SystemTime, const std::string&) override;

  void finishSpan() override;

  void injectContext(Tracing::TraceContext& trace_context,
                     const Upstream::HostDescriptionConstSharedPtr& upstream) override;

  Tracing::SpanPtr spawnChild(const Tracing::Config& config, const std::string& name,
                              SystemTime start_time) override;

  void setSampled(bool sampled) override;

  std::string getBaggage(absl::string_view key) override;

  void setBaggage(absl::string_view key, absl::string_view value) override;

  // Note that `getTraceIdAsHex` is nowhere used.
  std::string getTraceIdAsHex() const override;
};

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
