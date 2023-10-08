#pragma once

#include <string>
#include <vector>

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"
#include "source/extensions/filters/common/ratelimit/ratelimit.h"

#include "contrib/envoy/extensions/filters/network/generic_proxy/local_ratelimit/v3/local_ratelimit.pb.h"
#include "contrib/generic_proxy/filters/network/source/interface/filter.h"
#include "contrib/generic_proxy/filters/network/source/interface/stream.h"
#include "contrib/generic_proxy/filters/network/source/match.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace LocalRateLimit {

using UnitTypeProto =
    envoy::extensions::filters::network::generic_proxy::local_ratelimit::v3::UnitType;
using RateLimiterProto =
    envoy::extensions::filters::network::generic_proxy::local_ratelimit::v3::RateLimiter;
using RateLimitEntryProto =
    envoy::extensions::filters::network::generic_proxy::local_ratelimit::v3::RateLimitEntry;
using RateLimitConfig =
    envoy::extensions::filters::network::generic_proxy::local_ratelimit::v3::LocalRateLimit;

/**
 * All local rate limit stats. @see stats_macros.h
 */
#define ALL_LOCAL_RATE_LIMIT_STATS(COUNTER)                                                        \
  COUNTER(rate_limited)                                                                            \
  COUNTER(ok)

/**
 * Struct definition for all local rate limit stats. @see stats_macros.h
 */
struct LocalRateLimitStats {
  ALL_LOCAL_RATE_LIMIT_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Initializes the local rate limit filter configuration
 */
class LocalRateLimiterImpl {
public:
  LocalRateLimiterImpl(Protobuf::RepeatedPtrField<RateLimitEntryProto>& rateLimitEntries,
                       Envoy::Event::Dispatcher& dispatcher);
  ~LocalRateLimiterImpl() { rate_limit_entries_.clear(); }

  bool requestAllowed(const Request& request);

private:
  struct RateLimitEntry {
    std::unique_ptr<RequestMatchInputMatcher> match_;
    std::unique_ptr<Filters::Common::LocalRateLimit::LocalRateLimiterImpl> limiter_;
  };

  std::vector<std::unique_ptr<RateLimitEntry>> rate_limit_entries_;
};

/**
 * Global configuration for the local rate limit filter.
 */
class LocalRateLimitFilter : public DecoderFilter, Logger::Loggable<Logger::Id::filter> {
public:
  LocalRateLimitFilter(const RateLimitConfig& config,
                       Server::Configuration::FactoryContext& context);
  ~LocalRateLimitFilter() {
    // Ensure that the LocalRateLimiterImpl instance will be destroyed on the thread where its inner
    // timer is created and running.
    auto shared_ptr_wrapper =
        std::make_shared<std::unique_ptr<LocalRateLimiterImpl>>(std::move(rate_limiter_));
    dispatcher_.post([shared_ptr_wrapper]() { shared_ptr_wrapper->reset(); });
  }

  void onDestroy() override;
  // DecoderFilter
  void setDecoderFilterCallbacks(DecoderFilterCallback& decoder_callbacks) override {
    decoder_callbacks_ = &decoder_callbacks;
  }
  FilterStatus onStreamDecoded(Request& request) override;
  LocalRateLimitStats& stats() const { return stats_; }

private:
  void cleanup();
  static LocalRateLimitStats generateStats(const std::string& prefix, Stats::Scope& scope);

  DecoderFilterCallback* decoder_callbacks_{};
  Envoy::Event::Dispatcher& dispatcher_;
  mutable LocalRateLimitStats stats_;

  Protobuf::RepeatedPtrField<RateLimitEntryProto> rate_limits_;
  std::unique_ptr<LocalRateLimiterImpl> rate_limiter_;
};

} // namespace LocalRateLimit
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
