#pragma once

#include <cstdint>
#include <string>

#include "envoy/event/timer.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/router/router.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/upstream.h"

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
  uint32_t current_retry_{};
  DoRetryCallback callback_;
  Event::TimerPtr retry_timer_;
  Upstream::ResourcePriority priority_;
};

} // namespace Router
} // namespace Envoy
