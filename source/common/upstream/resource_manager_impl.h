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

struct ManagedResourceImpl : public BasicResourceLimitImpl {
  ManagedResourceImpl(uint64_t max, Runtime::Loader& runtime, const std::string& runtime_key,
                      Stats::Gauge& open_gauge, Stats::Gauge& remaining)
      : BasicResourceLimitImpl(max, runtime, runtime_key), open_gauge_(open_gauge),
        remaining_(remaining) {
    remaining_.set(max);
  }

  // BasicResourceLimitImpl
  bool canCreate() override { return current_ < max(); }
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
   * to notify that the circuit breaker is not yet triggered.
   */
  Stats::Gauge& open_gauge_;

  /**
   * The number of resources remaining before the circuit breaker opens.
   */
  Stats::Gauge& remaining_;
};

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
                      ClusterCircuitBreakersStats cb_stats, absl::optional<double> budget_percent,
                      absl::optional<uint32_t> min_retry_concurrency)
      : connections_(max_connections, runtime, runtime_key + "max_connections", cb_stats.cx_open_,
                     cb_stats.remaining_cx_),
        pending_requests_(max_pending_requests, runtime, runtime_key + "max_pending_requests",
                          cb_stats.rq_pending_open_, cb_stats.remaining_pending_),
        requests_(max_requests, runtime, runtime_key + "max_requests", cb_stats.rq_open_,
                  cb_stats.remaining_rq_),
        connection_pools_(max_connection_pools, runtime, runtime_key + "max_connection_pools",
                          cb_stats.cx_pool_open_, cb_stats.remaining_cx_pools_),
        retries_(budget_percent, min_retry_concurrency, max_retries, runtime,
                 runtime_key + "retry_budget.", runtime_key + "max_retries",
                 cb_stats.rq_retry_open_, cb_stats.remaining_retries_, requests_,
                 pending_requests_) {}

  // Upstream::ResourceManager
  ResourceLimit& connections() override { return connections_; }
  ResourceLimit& pendingRequests() override { return pending_requests_; }
  ResourceLimit& requests() override { return requests_; }
  ResourceLimit& retries() override { return retries_; }
  ResourceLimit& connectionPools() override { return connection_pools_; }

private:
  class RetryBudgetImpl : public ResourceLimit {
  public:
    RetryBudgetImpl(absl::optional<double> budget_percent,
                    absl::optional<uint32_t> min_retry_concurrency, uint64_t max_retries,
                    Runtime::Loader& runtime, const std::string& retry_budget_runtime_key,
                    const std::string& max_retries_runtime_key, Stats::Gauge& open_gauge,
                    Stats::Gauge& remaining, const ResourceLimit& requests,
                    const ResourceLimit& pending_requests)
        : runtime_(runtime),
          max_retry_resource_(max_retries, runtime, max_retries_runtime_key, open_gauge, remaining),
          budget_percent_(budget_percent), min_retry_concurrency_(min_retry_concurrency),
          budget_percent_key_(retry_budget_runtime_key + "budget_percent"),
          min_retry_concurrency_key_(retry_budget_runtime_key + "min_retry_concurrency"),
          requests_(requests), pending_requests_(pending_requests), remaining_(remaining) {}

    // Envoy::ResourceLimit
    bool canCreate() override {
      if (!useRetryBudget()) {
        return max_retry_resource_.canCreate();
      }
      clearRemainingGauge();
      return count() < max();
    }
    void inc() override {
      max_retry_resource_.inc();
      clearRemainingGauge();
    }
    void dec() override {
      max_retry_resource_.dec();
      clearRemainingGauge();
    }
    void decBy(uint64_t amount) override {
      max_retry_resource_.decBy(amount);
      clearRemainingGauge();
    }
    uint64_t max() override {
      if (!useRetryBudget()) {
        return max_retry_resource_.max();
      }

      const uint64_t current_active = requests_.count() + pending_requests_.count();
      const double budget_percent = runtime_.snapshot().getDouble(
          budget_percent_key_, budget_percent_ ? *budget_percent_ : 20.0);
      const uint32_t min_retry_concurrency = runtime_.snapshot().getInteger(
          min_retry_concurrency_key_, min_retry_concurrency_ ? *min_retry_concurrency_ : 3);

      clearRemainingGauge();

      // We enforce that the retry concurrency is never allowed to go below the
      // min_retry_concurrency, even if the configured percent of the current active requests
      // yields a value that is smaller.
      return std::max<uint64_t>(budget_percent / 100.0 * current_active, min_retry_concurrency);
    }
    uint64_t count() const override { return max_retry_resource_.count(); }

  private:
    bool useRetryBudget() const {
      return runtime_.snapshot().get(budget_percent_key_).has_value() ||
             runtime_.snapshot().get(min_retry_concurrency_key_).has_value() || budget_percent_ ||
             min_retry_concurrency_;
    }

    // If the retry budget is in use, the stats tracking remaining retries do not make sense since
    // they would dependent on other resources that can change without a call to this object.
    // Therefore, the gauge should just be reset to 0.
    void clearRemainingGauge() {
      if (useRetryBudget()) {
        remaining_.set(0);
      }
    }

    Runtime::Loader& runtime_;
    // The max_retry resource is nested within the budget to maintain state if the retry budget is
    // toggled.
    ManagedResourceImpl max_retry_resource_;
    const absl::optional<double> budget_percent_;
    const absl::optional<uint32_t> min_retry_concurrency_;
    const std::string budget_percent_key_;
    const std::string min_retry_concurrency_key_;
    const ResourceLimit& requests_;
    const ResourceLimit& pending_requests_;
    Stats::Gauge& remaining_;
  };

  ManagedResourceImpl connections_;
  ManagedResourceImpl pending_requests_;
  ManagedResourceImpl requests_;
  ManagedResourceImpl connection_pools_;
  RetryBudgetImpl retries_;
};

using ResourceManagerImplPtr = std::unique_ptr<ResourceManagerImpl>;

} // namespace Upstream
} // namespace Envoy
