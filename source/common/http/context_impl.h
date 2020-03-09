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
  explicit ContextImpl(Stats::SymbolTable& symbol_table);
  ~ContextImpl() override = default;

  const envoy::config::trace::v3::Tracing& defaultTracingConfig() override {
    return default_tracing_config_;
  }

  CodeStats& codeStats() override { return code_stats_; }

  void setDefaultTracingConfig(const envoy::config::trace::v3::Tracing& tracing_config) {
    default_tracing_config_ = tracing_config;
  }

private:
  envoy::config::trace::v3::Tracing default_tracing_config_;
  Http::CodeStatsImpl code_stats_;
};

} // namespace Http
} // namespace Envoy
