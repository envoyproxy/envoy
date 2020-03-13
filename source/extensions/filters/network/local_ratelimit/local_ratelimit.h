#pragma once

#include "envoy/event/timer.h"
#include "envoy/extensions/filters/network/local_ratelimit/v3/local_rate_limit.pb.h"
#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/thread_synchronizer.h"
#include "common/runtime/runtime_protos.h"

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

  // TODO(mattklein123): Determine if/how to merge this with token_bucket_impl.h/cc. This
  // implementation is geared towards multi-threading as well assumes a high call rate (which is
  // why a fixed periodic refresh timer is used).
  const Event::TimerPtr fill_timer_;
  const uint32_t max_tokens_;
  const uint32_t tokens_per_fill_;
  const std::chrono::milliseconds fill_interval_;
  Runtime::FeatureFlag enabled_;
  LocalRateLimitStats stats_;
  std::atomic<uint32_t> tokens_;
  Thread::ThreadSynchronizer synchronizer_; // Used for testing only.

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
