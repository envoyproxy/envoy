#pragma once

#include <cstdint>
#include <string>

#include "envoy/event/timer.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/router/router.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/upstream.h"

#include "common/common/backoff_strategy.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Router {

/**
 * Wraps retry state for the router.
 */
class RetryStateImpl : public RetryState {
public:
  static RetryStatePtr create(const RetryPolicy& route_policy, Http::HeaderMap& request_headers,
                              const Upstream::ClusterInfo& cluster, Runtime::Loader& runtime,
                              Runtime::RandomGenerator& random, Event::Dispatcher& dispatcher,
                              Upstream::ResourcePriority priority);
  ~RetryStateImpl();

  static uint32_t parseRetryOn(absl::string_view config);

  // Returns the RetryPolicy extracted from the x-envoy-retry-grpc-on header.
  static uint32_t parseRetryGrpcOn(absl::string_view retry_grpc_on_header);

  // Router::RetryState
  bool enabled() override { return retry_on_ != 0; }
  RetryStatus shouldRetry(const Http::HeaderMap* response_headers,
                          const absl::optional<Http::StreamResetReason>& reset_reason,
                          DoRetryCallback callback) override;
  bool shouldSelectAnotherHost(const Upstream::Host& host) override {
    return std::any_of(
        retry_host_predicates_.begin(), retry_host_predicates_.end(),
        [&host](auto predicate) { return predicate->shouldSelectAnotherHost(host); });
  }

  void onHostAttempted(Upstream::HostDescriptionConstSharedPtr host) override {
    std::for_each(retry_host_predicates_.begin(), retry_host_predicates_.end(),
                  [&host](auto predicate) { predicate->onHostAttempted(host); });
  }

private:
  RetryStateImpl(const RetryPolicy& route_policy, Http::HeaderMap& request_headers,
                 const Upstream::ClusterInfo& cluster, Runtime::Loader& runtime,
                 Runtime::RandomGenerator& random, Event::Dispatcher& dispatcher,
                 Upstream::ResourcePriority priority);

  void enableBackoffTimer();
  void resetRetry();
  bool wouldRetry(const Http::HeaderMap* response_headers,
                  const absl::optional<Http::StreamResetReason>& reset_reason);

  const Upstream::ClusterInfo& cluster_;
  Runtime::Loader& runtime_;
  Runtime::RandomGenerator& random_;
  Event::Dispatcher& dispatcher_;
  uint32_t retry_on_{};
  uint32_t retries_remaining_{1};
  DoRetryCallback callback_;
  Event::TimerPtr retry_timer_;
  Upstream::ResourcePriority priority_;
  BackOffStrategyPtr backoff_strategy_;
  std::vector<Upstream::RetryHostPredicateSharedPtr> retry_host_predicates_;
};

} // namespace Router
} // namespace Envoy
