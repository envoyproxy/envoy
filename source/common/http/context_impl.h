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
  ContextImpl();
  virtual ~ContextImpl();

  Tracing::HttpTracer& tracer() override { return *tracer_; }
  CodeStats& codeStats() override { return code_stats_; }

  void setTracer(Tracing::HttpTracer& tracer) { tracer_ = &tracer; }

private:
  Tracing::HttpTracerPtr tracer_storage_;
  Tracing::HttpTracer* tracer_;
  Http::CodeStatsImpl code_stats_;
};

} // namespace Http
} // namespace Envoy
