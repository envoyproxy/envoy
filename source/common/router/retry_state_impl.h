#pragma once

#include <cstdint>
#include <string>

#include "envoy/common/random_generator.h"
#include "envoy/event/timer.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/router/context.h"
#include "envoy/router/router.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/backoff_strategy.h"
#include "source/common/http/header_utility.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Router {

/**
 * Wraps retry state for the router.
 */
class RetryStateImpl : public RetryState {
public:
  static std::unique_ptr<RetryStateImpl>
  create(const RetryPolicy& route_policy, Http::RequestHeaderMap& request_headers,
         const Upstream::ClusterInfo& cluster, const VirtualCluster* vcluster,
         RouteStatsContextOptRef route_stats_context, Runtime::Loader& runtime,
         Random::RandomGenerator& random, Event::Dispatcher& dispatcher, TimeSource& time_source,
         Upstream::ResourcePriority priority);
  ~RetryStateImpl() override;

  /**
   * Returns the RetryPolicy extracted from the x-envoy-retry-on header.
   * @param config is the value of the header.
   * @return std::pair<uint32_t, bool> the uint32_t is a bitset representing the
   *         valid retry policies in @param config. The bool is TRUE iff all the
   *         policies specified in @param config are valid.
   */
  static std::pair<uint32_t, bool> parseRetryOn(absl::string_view config);

  /**
   * Returns the RetryPolicy extracted from the x-envoy-retry-grpc-on header.
   * @param config is the value of the header.
   * @return std::pair<uint32_t, bool> the uint32_t is a bitset representing the
   *         valid retry policies in @param config. The bool is TRUE iff all the
   *         policies specified in @param config are valid.
   */
  static std::pair<uint32_t, bool> parseRetryGrpcOn(absl::string_view retry_grpc_on_header);

  // Router::RetryState
  bool enabled() override { return retry_on_ != 0; }
  absl::optional<std::chrono::milliseconds>
  parseResetInterval(const Http::ResponseHeaderMap& response_headers) const override;
  RetryStatus shouldRetryHeaders(const Http::ResponseHeaderMap& response_headers,
                                 const Http::RequestHeaderMap& original_request,
                                 DoRetryHeaderCallback callback) override;
  // Returns if the retry policy would retry the passed headers and how. Does not
  // take into account circuit breaking or remaining tries.
  RetryDecision wouldRetryFromHeaders(const Http::ResponseHeaderMap& response_headers,
                                      const Http::RequestHeaderMap& original_request,
                                      bool& disable_early_data) override;
  RetryStatus shouldRetryReset(Http::StreamResetReason reset_reason, Http3Used http3_used,
                               DoRetryResetCallback callback) override;
  RetryStatus shouldHedgeRetryPerTryTimeout(DoRetryCallback callback) override;

  void onHostAttempted(Upstream::HostDescriptionConstSharedPtr host) override {
    std::for_each(retry_host_predicates_.begin(), retry_host_predicates_.end(),
                  [&host](auto predicate) { predicate->onHostAttempted(host); });
    if (retry_priority_) {
      retry_priority_->onHostAttempted(host);
    }
  }

  bool shouldSelectAnotherHost(const Upstream::Host& host) override {
    return std::any_of(
        retry_host_predicates_.begin(), retry_host_predicates_.end(),
        [&host](auto predicate) { return predicate->shouldSelectAnotherHost(host); });
  }

  const Upstream::HealthyAndDegradedLoad& priorityLoadForRetry(
      const Upstream::PrioritySet& priority_set,
      const Upstream::HealthyAndDegradedLoad& original_priority_load,
      const Upstream::RetryPriority::PriorityMappingFunc& priority_mapping_func) override {
    if (!retry_priority_) {
      return original_priority_load;
    }
    return retry_priority_->determinePriorityLoad(priority_set, original_priority_load,
                                                  priority_mapping_func);
  }

  uint32_t hostSelectionMaxAttempts() const override { return host_selection_max_attempts_; }

  bool isAutomaticallyConfiguredForHttp3() const { return auto_configured_for_http3_; }

private:
  RetryStateImpl(const RetryPolicy& route_policy, Http::RequestHeaderMap& request_headers,
                 const Upstream::ClusterInfo& cluster, const VirtualCluster* vcluster,
                 RouteStatsContextOptRef route_stats_context, Runtime::Loader& runtime,
                 Random::RandomGenerator& random, Event::Dispatcher& dispatcher,
                 TimeSource& time_source, Upstream::ResourcePriority priority,
                 bool auto_configured_for_http3,
                 bool conn_pool_new_stream_with_early_data_and_http3);

  void enableBackoffTimer();
  void resetRetry();
  // Returns if the retry policy would retry the reset and how. Does not
  // take into account circuit breaking or remaining tries.
  // disable_http3: populated to tell the caller whether to disable http3 or not when the return
  // value indicates retry.
  RetryDecision wouldRetryFromReset(const Http::StreamResetReason reset_reason,
                                    Http3Used http3_used, bool& disable_http3);
  RetryStatus shouldRetry(RetryDecision would_retry, DoRetryCallback callback);

  const Upstream::ClusterInfo& cluster_;
  const VirtualCluster* vcluster_;
  RouteStatsContextOptRef route_stats_context_;
  Runtime::Loader& runtime_;
  Random::RandomGenerator& random_;
  Event::Dispatcher& dispatcher_;
  TimeSource& time_source_;
  uint32_t retry_on_{};
  uint32_t retries_remaining_{};
  DoRetryCallback backoff_callback_;
  Event::SchedulableCallbackPtr next_loop_callback_;
  Event::TimerPtr retry_timer_;
  Upstream::ResourcePriority priority_;
  BackOffStrategyPtr backoff_strategy_;
  BackOffStrategyPtr ratelimited_backoff_strategy_{};
  std::vector<Upstream::RetryHostPredicateSharedPtr> retry_host_predicates_;
  Upstream::RetryPrioritySharedPtr retry_priority_;
  uint32_t host_selection_max_attempts_;
  std::vector<uint32_t> retriable_status_codes_;
  std::vector<Http::HeaderMatcherSharedPtr> retriable_headers_;
  std::vector<ResetHeaderParserSharedPtr> reset_headers_{};
  std::chrono::milliseconds reset_max_interval_{};
  const bool auto_configured_for_http3_{};
  const bool conn_pool_new_stream_with_early_data_and_http3_{};
};

} // namespace Router
} // namespace Envoy
