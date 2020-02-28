#pragma once

#include "envoy/http/context.h"

#include "common/http/codes.h"
#include "common/http/user_agent.h"
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
  const UserAgentContext& userAgentContext() const override { return user_agent_context_; }

private:
  Tracing::HttpNullTracer null_tracer_;
  Tracing::HttpTracer* tracer_;
  CodeStatsImpl code_stats_;
  UserAgentContext user_agent_context_;
};

} // namespace Http
} // namespace Envoy
