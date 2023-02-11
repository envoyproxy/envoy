#pragma once

#include "envoy/event/file_event.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/filters/listener/local_ratelimit/v3/local_ratelimit.pb.h"
#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/common/runtime/runtime_protos.h"
#include "source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace LocalRateLimit {

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

using LocalRateLimiterImplSharedPtr =
    std::shared_ptr<Filters::Common::LocalRateLimit::LocalRateLimiterImpl>;

/**
 * Configuration shared across all connections. Must be thread safe.
 */
class Config : Logger::Loggable<Logger::Id::filter> {
public:
  Config(
      const envoy::extensions::filters::listener::local_ratelimit::v3::LocalRateLimit& proto_config,
      Event::Dispatcher& dispatcher, Stats::Scope& scope, Runtime::Loader& runtime);

  ~Config() = default;

  bool canCreateConnection();
  bool enabled() { return enabled_.enabled(); }
  const LocalRateLimitStats& stats() const { return stats_; }

private:
  static LocalRateLimitStats generateStats(const std::string& prefix, Stats::Scope& scope);

  Runtime::FeatureFlag enabled_;
  LocalRateLimitStats stats_;
  LocalRateLimiterImplSharedPtr rate_limiter_;
};

using ConfigSharedPtr = std::shared_ptr<Config>;

/**
 * Local Rate Limit listener filter.
 */
class Filter : public Network::ListenerFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(const ConfigSharedPtr& config) : config_(config) {}

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override;
  Network::FilterStatus onData(Network::ListenerFilterBuffer&) override {
    return Network::FilterStatus::Continue;
  }
  size_t maxReadBytes() const override { return 0; }

private:
  ConfigSharedPtr config_;
};

} // namespace LocalRateLimit
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
