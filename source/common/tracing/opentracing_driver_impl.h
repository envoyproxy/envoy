#pragma once

#include <memory>

#include "envoy/tracing/http_tracer.h"

#include "common/common/logger.h"

#include "opentracing/tracer.h"

namespace Envoy {
namespace Tracing {

class OpenTracingSpan : public Span, Logger::Loggable<Logger::Id::tracing> {
public:
  OpenTracingSpan(bool use_single_header_propagation, bool use_tracer_propagation,
                  std::unique_ptr<opentracing::Span>&& span);

  // Tracing::Span
  void finishSpan() override;
  void setOperation(const std::string& operation) override;
  void setTag(const std::string& name, const std::string& value) override;
  void injectContext(Http::HeaderMap& request_headers) override;
  SpanPtr spawnChild(const Config& config, const std::string& name, SystemTime start_time) override;

private:
  const bool use_single_header_propagation_;
  const bool use_tracer_propagation_;
  std::unique_ptr<opentracing::Span> span_;
};

class OpenTracingDriver : public Driver, protected Logger::Loggable<Logger::Id::tracing> {
public:
  // Tracer::TracingDriver
  SpanPtr startSpan(const Config& config, Http::HeaderMap& request_headers,
                    const std::string& operation_name, SystemTime start_time) override;

  virtual const opentracing::Tracer& tracer() const PURE;

  virtual bool useSingleHeaderPropagation() const { return true; }
  virtual bool useTracerPropagation() const { return true; }
};

} // namespace Tracing
} // namespace Envoy
