#pragma once

#include "envoy/http/context.h"

#include "source/common/http/codes.h"
#include "source/common/http/user_agent.h"

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

  CodeStats& codeStats() override {
    if (use_explicit_tags_) {
      return tagged_code_stats_;
    }
    return code_stats_;
  }

  /**
   * Selects which CodeStats implementation codeStats() hands out. The explicit-tags implementation
   * attaches the response code, its class, the virtual host, the virtual cluster and the route to
   * each stat as a tag instead of leaving them to the tag extraction rules.
   */
  void setUseExplicitTags(bool use_explicit_tags) { use_explicit_tags_ = use_explicit_tags; }

  void setDefaultTracingConfig(const envoy::config::trace::v3::Tracing& tracing_config) {
    default_tracing_config_ = tracing_config;
  }

  const UserAgentContext& userAgentContext() const override { return user_agent_context_; }
  const Stats::StatName& asyncClientStatPrefix() const override {
    return async_client_stat_prefix_;
  }

private:
  CodeStatsImpl code_stats_;
  TaggedCodeStatsImpl tagged_code_stats_;
  bool use_explicit_tags_{false};
  UserAgentContext user_agent_context_;
  const Stats::StatName async_client_stat_prefix_;
  envoy::config::trace::v3::Tracing default_tracing_config_;
};

} // namespace Http
} // namespace Envoy
