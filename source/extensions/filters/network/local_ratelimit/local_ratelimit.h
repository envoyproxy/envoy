#pragma once

#include "envoy/event/timer.h"
#include "envoy/extensions/filters/network/local_ratelimit/v3/local_rate_limit.pb.h"
#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/thread_synchronizer.h"
#include "common/runtime/runtime_protos.h"

#include "extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace LocalRateLimitFilter {

/**
 * All local rate limit stats. @see stats_macros.h
 */
#define ALL_LOCAL_RATE_LIMIT_STATS(COUNTER) COUNTER(rate_limited)

/**
 * Struct definition for all local rate limit stats. @see stats_macros.h
 */
struct LocalRateLimitStats {
  ALL_LOCAL_RATE_LIMIT_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration shared across all connections. Must be thread safe.
 */
class Config : Logger::Loggable<Logger::Id::filter> {
public:
  Config(
      const envoy::extensions::filters::network::local_ratelimit::v3::LocalRateLimit& proto_config,
      Event::Dispatcher& dispatcher, Stats::Scope& scope, Runtime::Loader& runtime);

  bool canCreateConnection();
  bool enabled() { return enabled_.enabled(); }
  LocalRateLimitStats& stats() { return stats_; }

private:
  static LocalRateLimitStats generateStats(const std::string& prefix, Stats::Scope& scope);
  void onFillTimer();

  Filters::Common::LocalRateLimit::LocalRateLimiterImpl rate_limiter_;
  Runtime::FeatureFlag enabled_;
  LocalRateLimitStats stats_;

  friend class LocalRateLimitTestBase;
};

using ConfigSharedPtr = std::shared_ptr<Config>;

/**
 * Per-connection local rate limit filter.
 */
class Filter : public Network::ReadFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(const ConfigSharedPtr& config) : config_(config) {}

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance&, bool) override {
    return Network::FilterStatus::Continue;
  }
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& read_callbacks) override {
    read_callbacks_ = &read_callbacks;
  }

private:
  const ConfigSharedPtr config_;
  Network::ReadFilterCallbacks* read_callbacks_{};
};

} // namespace LocalRateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
