#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/resource.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/resource_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/common/assert.h"
#include "common/common/basic_resource_impl.h"

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
                      uint64_t max_requests, uint64_t max_retries, uint64_t max_connection_pools,
                      ClusterCircuitBreakersStats cb_stats)
      : connections_(max_connections, runtime, runtime_key + "max_connections", cb_stats.cx_open_,
                     cb_stats.remaining_cx_),
        pending_requests_(max_pending_requests, runtime, runtime_key + "max_pending_requests",
                          cb_stats.rq_pending_open_, cb_stats.remaining_pending_),
        requests_(max_requests, runtime, runtime_key + "max_requests", cb_stats.rq_open_,
                  cb_stats.remaining_rq_),
        retries_(max_retries, runtime, runtime_key + "max_retries", cb_stats.rq_retry_open_,
                 cb_stats.remaining_retries_),
        connection_pools_(max_connection_pools, runtime, runtime_key + "max_connection_pools",
                          cb_stats.cx_pool_open_, cb_stats.remaining_cx_pools_) {}

  // Upstream::ResourceManager
  ResourceLimit& connections() override { return connections_; }
  ResourceLimit& pendingRequests() override { return pending_requests_; }
  ResourceLimit& requests() override { return requests_; }
  ResourceLimit& retries() override { return retries_; }
  ResourceLimit& connectionPools() override { return connection_pools_; }

private:
  struct ManagedResourceImpl : public BasicResourceLimitImpl {
    ManagedResourceImpl(uint64_t max, Runtime::Loader& runtime, const std::string& runtime_key,
                        Stats::Gauge& open_gauge, Stats::Gauge& remaining)
        : BasicResourceLimitImpl(max, runtime, runtime_key), open_gauge_(open_gauge),
          remaining_(remaining) {
      remaining_.set(max);
    }

    ~ManagedResourceImpl() override { ASSERT(count() == 0); }

    void inc() override {
      BasicResourceLimitImpl::inc();
      updateRemaining();
      open_gauge_.set(BasicResourceLimitImpl::canCreate() ? 0 : 1);
    }

    void decBy(uint64_t amount) override {
      BasicResourceLimitImpl::decBy(amount);
      updateRemaining();
      open_gauge_.set(BasicResourceLimitImpl::canCreate() ? 0 : 1);
    }

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
      const uint64_t current_copy = current_;
      remaining_.set(max() > current_copy ? max() - current_copy : 0);
    }

    /**
     * A gauge to notify the live circuit breaker state. The gauge is set to 0
     * to notify that the circuit breaker is closed, or to 1 to notify that it
     * is open.
     */
    Stats::Gauge& open_gauge_;

    /**
     * The number of resources remaining before the circuit breaker opens.
     */
    Stats::Gauge& remaining_;
  };

  ManagedResourceImpl connections_;
  ManagedResourceImpl pending_requests_;
  ManagedResourceImpl requests_;
  ManagedResourceImpl retries_;
  ManagedResourceImpl connection_pools_;
};

using ResourceManagerImplPtr = std::unique_ptr<ResourceManagerImpl>;

} // namespace Upstream
} // namespace Envoy
