#pragma once

#include "envoy/http/context.h"

#include "common/http/codes.h"
#include "common/http/user_agent.h"

namespace Envoy {
namespace Http {

/**
 * Captures http-related structures with cardinality of one per server.
 */
class ContextImpl : public Context {
public:
  explicit ContextImpl(Stats::SymbolTable& symbol_table);
  ~ContextImpl() override = default;

  const envoy::config::trace::v3::Tracing& defaultTracingConfig() override {
    return default_tracing_config_;
  }

  CodeStats& codeStats() override { return code_stats_; }

  void setDefaultTracingConfig(const envoy::config::trace::v3::Tracing& tracing_config) {
    default_tracing_config_ = tracing_config;
  }

  const UserAgentContext& userAgentContext() const override { return user_agent_context_; }

private:
  CodeStatsImpl code_stats_;
  UserAgentContext user_agent_context_;
  envoy::config::trace::v3::Tracing default_tracing_config_;
};

} // namespace Http
} // namespace Envoy
