#pragma once

#include "envoy/http/context.h"

#include "common/http/codes.h"
#include "common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Http {

/**
 * Captures http-related structures with cardinality of one per server.
 */
class ContextImpl : public Context {
public:
  explicit ContextImpl(Stats::SymbolTable& symbol_table);
  ~ContextImpl() override = default;

  Tracing::HttpTracer& tracer() override { return *tracer_; }
  CodeStats& codeStats() override { return code_stats_; }

  void setTracer(Tracing::HttpTracer& tracer) { tracer_ = &tracer; }

private:
  Tracing::HttpNullTracer null_tracer_;
  Tracing::HttpTracer* tracer_;
  Http::CodeStatsImpl code_stats_;
};

} // namespace Http
} // namespace Envoy
