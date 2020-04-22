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
  Resource& connections() override { return connections_; }
  Resource& pendingRequests() override { return pending_requests_; }
  Resource& requests() override { return requests_; }
  Resource& retries() override { return retries_; }
  Resource& connectionPools() override { return connection_pools_; }

private:
  struct ResourceImpl : public Resource {
    ResourceImpl(uint64_t max, Runtime::Loader& runtime, const std::string& runtime_key,
                 Stats::Gauge& open_gauge, Stats::Gauge& remaining)
        : max_(max), runtime_(runtime), runtime_key_(runtime_key), open_gauge_(open_gauge),
          remaining_(remaining) {
      remaining_.set(max);
    }
    ~ResourceImpl() override { ASSERT(current_ == 0); }

    // Upstream::Resource
    bool canCreate() override { return current_ < max(); }
    void inc() override {
      current_++;
      updateRemaining();
      open_gauge_.set(canCreate() ? 0 : 1);
    }
    void dec() override { decBy(1); }
    void decBy(uint64_t amount) override {
      ASSERT(current_ >= amount);
      current_ -= amount;
      updateRemaining();
      open_gauge_.set(canCreate() ? 0 : 1);
    }
    uint64_t max() override { return runtime_.snapshot().getInteger(runtime_key_, max_); }
    uint64_t count() const override { return current_.load(); }

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

    const uint64_t max_;
    std::atomic<uint64_t> current_{};
    Runtime::Loader& runtime_;
    const std::string runtime_key_;

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

  class RetryBudgetImpl : public Resource {
  public:
    RetryBudgetImpl(absl::optional<double> budget_percent,
                    absl::optional<uint32_t> min_retry_concurrency, uint64_t max_retries,
                    Runtime::Loader& runtime, const std::string& retry_budget_runtime_key,
                    const std::string& max_retries_runtime_key, Stats::Gauge& open_gauge,
                    Stats::Gauge& remaining, const Resource& requests,
                    const Resource& pending_requests)
        : runtime_(runtime),
          max_retry_resource_(max_retries, runtime, max_retries_runtime_key, open_gauge, remaining),
          budget_percent_(budget_percent), min_retry_concurrency_(min_retry_concurrency),
          budget_percent_key_(retry_budget_runtime_key + "budget_percent"),
          min_retry_concurrency_key_(retry_budget_runtime_key + "min_retry_concurrency"),
          requests_(requests), pending_requests_(pending_requests), remaining_(remaining) {}

    // Upstream::Resource
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
    ResourceImpl max_retry_resource_;
    const absl::optional<double> budget_percent_;
    const absl::optional<uint32_t> min_retry_concurrency_;
    const std::string budget_percent_key_;
    const std::string min_retry_concurrency_key_;
    const Resource& requests_;
    const Resource& pending_requests_;
    Stats::Gauge& remaining_;
  };

  ResourceImpl connections_;
  ResourceImpl pending_requests_;
  ResourceImpl requests_;
  ResourceImpl connection_pools_;
  RetryBudgetImpl retries_;
};

using ResourceManagerImplPtr = std::unique_ptr<ResourceManagerImpl>;

} // namespace Upstream
} // namespace Envoy
