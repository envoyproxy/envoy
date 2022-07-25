#pragma once

#include "envoy/event/timer.h"
#include "envoy/extensions/filters/network/local_ratelimit/v3/local_rate_limit.pb.h"
#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/singleton/manager.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/thread_synchronizer.h"
#include "source/common/runtime/runtime_protos.h"
#include "source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"

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

using LocalRateLimiterImplSharedPtr =
    std::shared_ptr<Filters::Common::LocalRateLimit::LocalRateLimiterImpl>;

class SharedRateLimitSingleton : public Singleton::Instance, Logger::Loggable<Logger::Id::filter> {
public:
  ~SharedRateLimitSingleton() override;

  class Key : public std::pair<std::string, envoy::type::v3::TokenBucket> {
  public:
    using std::pair<std::string, envoy::type::v3::TokenBucket>::pair;

    // Add absl::Hash support.
    template <typename H>
    friend H AbslHashValue(H h, const Key& key) { // NOLINT(readability-identifier-naming)
      return H::combine(std::move(h), key.first, MessageUtil::hash(key.second));
    }

    bool operator==(const Key& that) const {
      return (this->first == that.first) &&
             Protobuf::util::MessageDifferencer::Equivalent(this->second, that.second);
    }
  };

  std::pair<LocalRateLimiterImplSharedPtr, const Key*>
  get(const envoy::extensions::filters::network::local_ratelimit::v3::LocalRateLimit& proto_config,
      std::function<LocalRateLimiterImplSharedPtr()> create_fn);

  void removeIfUnused(const Key* key);

private:
  // Use node_hash_map so that a stable pointer to Key can be returned.
  absl::node_hash_map<Key, std::weak_ptr<Filters::Common::LocalRateLimit::LocalRateLimiterImpl>>
      limiters_;
};

/**
 * Configuration shared across all connections. Must be thread safe.
 */
class Config : Logger::Loggable<Logger::Id::filter> {
public:
  Config(
      const envoy::extensions::filters::network::local_ratelimit::v3::LocalRateLimit& proto_config,
      Event::Dispatcher& dispatcher, Stats::Scope& scope, Runtime::Loader& runtime,
      Singleton::Manager& singleton_manager);

  ~Config();

  bool canCreateConnection();
  bool enabled() { return enabled_.enabled(); }
  LocalRateLimitStats& stats() { return stats_; }

private:
  static LocalRateLimitStats generateStats(const std::string& prefix, Stats::Scope& scope);

  LocalRateLimiterImplSharedPtr rate_limiter_;
  Runtime::FeatureFlag enabled_;
  LocalRateLimitStats stats_;
  const SharedRateLimitSingleton::Key* shared_bucket_key_{};
  std::shared_ptr<SharedRateLimitSingleton> shared_bucket_registry_;

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
