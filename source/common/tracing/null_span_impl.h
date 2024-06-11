#pragma once

#include "envoy/tracing/trace_driver.h"

#include "source/common/common/empty_string.h"

namespace Envoy {
namespace Tracing {

/**
 * Null implementation of Span.
 */
class NullSpan : public Span {
public:
  static NullSpan& instance() {
    static NullSpan* instance = new NullSpan();
    return *instance;
  }

  // Tracing::Span
  void setOperation(absl::string_view) override {}
  void setTag(absl::string_view, absl::string_view) override {}
  void log(SystemTime, const std::string&) override {}
  void finishSpan() override {}
  void injectContext(Tracing::TraceContext&, const UpstreamContext&) override {}
  void setBaggage(absl::string_view, absl::string_view) override {}
  std::string getBaggage(absl::string_view) override { return EMPTY_STRING; }
  std::string getSpanId() const override { return EMPTY_STRING; }
  std::string getTraceId() const override { return EMPTY_STRING; }
  SpanPtr spawnChild(const Config&, const std::string&, SystemTime) override {
    return SpanPtr{new NullSpan()};
  }
  void setSampled(bool) override {}
};

} // namespace Tracing
} // namespace Envoy
