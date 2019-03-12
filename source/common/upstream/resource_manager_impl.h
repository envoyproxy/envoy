#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/runtime/runtime.h"
#include "envoy/upstream/resource_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Upstream {

/**
 * Implementation of ResourceManager.
 * NOTE: This implementation makes some assumptions which favor simplicity over correctness.
 * 1) Primarily, it assumes that traffic will be mostly balanced over all the worker threads since
 *    no attempt is made to balance resources between them. It is possible that starvation can
 *    occur during high contention.
 * 2) Though atomics are used, it is possible for resources to temporarily go above the supplied
 *    maximums. This should not effect overall behavior.
 */
class ResourceManagerImpl : public ResourceManager {
public:
  ResourceManagerImpl(Runtime::Loader& runtime, const std::string& runtime_key,
                      uint64_t max_connections, uint64_t max_pending_requests,
                      uint64_t max_requests, uint64_t max_retries,
                      ClusterCircuitBreakersStats cb_stats)
      : connections_(max_connections, runtime, runtime_key + "max_connections", cb_stats.cx_open_,
                     cb_stats.remaining_cx_),
        pending_requests_(max_pending_requests, runtime, runtime_key + "max_pending_requests",
                          cb_stats.rq_pending_open_, cb_stats.remaining_pending_),
        requests_(max_requests, runtime, runtime_key + "max_requests", cb_stats.rq_open_,
                  cb_stats.remaining_rq_),
        retries_(max_retries, runtime, runtime_key + "max_retries", cb_stats.rq_retry_open_,
                 cb_stats.remaining_retries_) {}

  // Upstream::ResourceManager
  Resource& connections() override { return connections_; }
  Resource& pendingRequests() override { return pending_requests_; }
  Resource& requests() override { return requests_; }
  Resource& retries() override { return retries_; }

private:
  struct ResourceImpl : public Resource {
    ResourceImpl(uint64_t max, Runtime::Loader& runtime, const std::string& runtime_key,
                 Stats::BoolIndicator& circuit_breaker_open, Stats::Gauge& remaining)
        : max_(max), runtime_(runtime), runtime_key_(runtime_key),
          circuit_breaker_open_(circuit_breaker_open), remaining_(remaining) {
      remaining_.set(max);
    }
    ~ResourceImpl() { ASSERT(current_ == 0); }

    // Upstream::Resource
    bool canCreate() override { return current_ < max(); }
    void inc() override {
      current_++;
      circuit_breaker_open_.set(!canCreate());
      updateRemaining();
    }
    void dec() override {
      ASSERT(current_ > 0);
      current_--;
      circuit_breaker_open_.set(!canCreate());
      updateRemaining();
    }
    uint64_t max() override { return runtime_.snapshot().getInteger(runtime_key_, max_); }

    /**
     * We set the gauge instead of incrementing and decrementing because,
     * though atomics are used, it is possible for the current resource count
     * to be greater than the supplied max.
     */
    void updateRemaining() {
      /**
       * We cannot use std::max here because max() and current_ are
       * unsigned and subtracting them may overflow.
       */
      remaining_.set(max() > current_ ? max() - current_ : 0);
    }

    const uint64_t max_;
    std::atomic<uint64_t> current_{};
    Runtime::Loader& runtime_;
    const std::string runtime_key_;

    /**
     * The live circuit breaker state: false when the circuit breaker is closed,
     * true when open.
     */
    Stats::BoolIndicator& circuit_breaker_open_;

    /**
     * The number of resources remaining before the circuit breaker opens.
     */
    Stats::Gauge& remaining_;
  };

  ResourceImpl connections_;
  ResourceImpl pending_requests_;
  ResourceImpl requests_;
  ResourceImpl retries_;
};

typedef std::unique_ptr<ResourceManagerImpl> ResourceManagerImplPtr;

} // namespace Upstream
} // namespace Envoy
