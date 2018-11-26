#pragma once

#include "envoy/http/context.h"

#include "common/http/codes.h"

namespace Envoy {
namespace Http {

/**
 * Captures http-related structures with cardinality of one per server.
 */
class ContextImpl : public Context {
 public:
  // Default implementation uses a null tracer.
  ContextImpl();

  // Takes ownership of tracer.
  explicit ContextImpl(Tracing::HttpTracerPtr tracer);

  virtual ~ContextImpl();

  Tracing::HttpTracer& tracer() override { return *tracer_; }
  CodeStats& codeStats() override { return code_stats_; }

 private:
  Tracing::HttpTracerPtr tracer_;
  Http::CodeStatsImpl code_stats_;
};

} // namespace Http
} // namespace Envoy
