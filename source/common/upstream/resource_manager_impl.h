#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/resource.h"
#include "envoy/common/time.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/resource_manager.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/assert.h"
#include "source/common/common/basic_resource_impl.h"

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
  void inc() override {
    BasicResourceLimitImpl::inc();
    updateRemaining();
    open_gauge_.set(BasicResourceLimitImpl::canCreate() ? 0 : 1);
    if (on_inc_cb_) {
      on_inc_cb_();
    }
  }
  void decBy(uint64_t amount) override {
    BasicResourceLimitImpl::decBy(amount);
    updateRemaining();
    open_gauge_.set(BasicResourceLimitImpl::canCreate() ? 0 : 1);
  }

  // Optional callback function used for counting new requests in retry budget.
  std::function<void()> on_inc_cb_;

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
                      uint64_t max_connections_per_host, ClusterCircuitBreakersStats cb_stats,
                      absl::optional<double> budget_percent,
                      absl::optional<std::chrono::milliseconds> budget_interval,
                      absl::optional<uint32_t> min_retry_concurrency, TimeSource& time_source)
      : connections_(max_connections, runtime, runtime_key + "max_connections", cb_stats.cx_open_,
                     cb_stats.remaining_cx_),
        pending_requests_(max_pending_requests, runtime, runtime_key + "max_pending_requests",
                          cb_stats.rq_pending_open_, cb_stats.remaining_pending_),
        requests_(max_requests, runtime, runtime_key + "max_requests", cb_stats.rq_open_,
                  cb_stats.remaining_rq_),
        connection_pools_(max_connection_pools, runtime, runtime_key + "max_connection_pools",
                          cb_stats.cx_pool_open_, cb_stats.remaining_cx_pools_),
        max_connections_per_host_(max_connections_per_host),
        retries_(budget_percent, budget_interval, min_retry_concurrency, max_retries, runtime,
                 runtime_key + "retry_budget.", runtime_key + "max_retries",
                 cb_stats.rq_retry_open_, cb_stats.remaining_retries_, requests_, pending_requests_,
                 time_source) {
    // Count active requests when retry budget is configured.
    // Pending requests are not counted to avoid counting them twice.
    if (budget_percent.has_value() || min_retry_concurrency.has_value()) {
      requests_.on_inc_cb_ = [this]() { retries_.writeRequest(); };
    }
  }

  // Upstream::ResourceManager
  ResourceLimit& connections() override { return connections_; }
  ResourceLimit& pendingRequests() override { return pending_requests_; }
  ResourceLimit& requests() override { return requests_; }
  ResourceLimit& retries() override { return retries_; }
  ResourceLimit& connectionPools() override { return connection_pools_; }
  uint64_t maxConnectionsPerHost() override { return max_connections_per_host_; }

private:
  class RetryBudgetImpl : public ResourceLimit {
  public:
    RetryBudgetImpl(absl::optional<double> budget_percent,
                    absl::optional<std::chrono::milliseconds> budget_interval,
                    absl::optional<uint32_t> min_retry_concurrency, uint64_t max_retries,
                    Runtime::Loader& runtime, const std::string& retry_budget_runtime_key,
                    const std::string& max_retries_runtime_key, Stats::Gauge& open_gauge,
                    Stats::Gauge& remaining, const ResourceLimit& requests,
                    const ResourceLimit& pending_requests, TimeSource& time_source)
        : runtime_(runtime),
          max_retry_resource_(max_retries, runtime, max_retries_runtime_key, open_gauge, remaining),
          budget_percent_(budget_percent), budget_interval_(budget_interval),
          min_retry_concurrency_(min_retry_concurrency),
          budget_percent_key_(retry_budget_runtime_key + "budget_percent"),
          budget_interval_key_(retry_budget_runtime_key + "budget_interval"),
          min_retry_concurrency_key_(retry_budget_runtime_key + "min_retry_concurrency"),
          requests_(requests), pending_requests_(pending_requests), remaining_(remaining),
          time_source_(time_source), slots_(),
          slot_duration_seconds_(
              budget_interval.has_value()
                  ? std::chrono::duration<double>(budget_interval.value()).count() / windows
                  : 0),
          writer_(0),
          generation_time_seconds_(budget_interval.has_value() ? timeNowInSeconds() : 0),
          generation_index_(0) {}

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
    void writeRequest() {
      expire();
      writer_.fetch_add(1, std::memory_order_relaxed);
    }
    uint64_t max() override {
      if (!useRetryBudget()) {
        return max_retry_resource_.max();
      }

      const double budget_percent = runtime_.snapshot().getDouble(
          budget_percent_key_, budget_percent_ ? *budget_percent_ : 20.0);
      const uint64_t budget_interval = runtime_.snapshot().getInteger(
          budget_interval_key_,
          budget_interval_ ? static_cast<uint64_t>(budget_interval_->count()) : 0);
      const uint32_t min_retry_concurrency = runtime_.snapshot().getInteger(
          min_retry_concurrency_key_, min_retry_concurrency_ ? *min_retry_concurrency_ : 3);

      clearRemainingGauge();

      // Use the in-flight request count when budget_interval is explicitly set to 0.
      if (budget_interval == 0) {
        // We enforce that the retry concurrency is never allowed to go below the
        // min_retry_concurrency, even if the configured percent of the current active requests
        // yields a value that is smaller.
        const uint64_t current_active = requests_.count() + pending_requests_.count();
        return std::max<uint64_t>(budget_percent / 100.0 * current_active, min_retry_concurrency);
      }

      const uint64_t requests_for_budget = sumRequests();
      return std::max<uint64_t>(budget_percent / 100.0 * requests_for_budget,
                                min_retry_concurrency);
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

    double timeNowInSeconds() const {
      return std::chrono::duration<double>(time_source_.monotonicTime().time_since_epoch()).count();
    }

    uint64_t sumRequests() {
      expire();

      uint64_t sum = writer_.load(std::memory_order_seq_cst);
      for (const auto& slot : slots_) {
        sum += slot.load(std::memory_order_seq_cst);
      }
      return sum;
    }

    // Retry budget implementation across a fixed window heavily inspired by tower.rs:
    // https://github.com/tower-rs/tower/blob/master/tower/src/retry/budget/tps_budget.rs
    void expire() {
      const double now = timeNowInSeconds();
      double gen_time = generation_time_seconds_.load(std::memory_order_relaxed);
      double elapsed = now - gen_time;

      if (elapsed < slot_duration_seconds_) {
        return;
      }

      // Try to claim the generation if it is expired.
      if (!generation_time_seconds_.compare_exchange_strong(gen_time, now,
                                                            std::memory_order_relaxed)) {
        return;
      }

      // Commit the writer count into the current slot.
      uint32_t idx = generation_index_.load(std::memory_order_relaxed);
      const uint64_t to_commit = writer_.exchange(0, std::memory_order_relaxed);
      slots_[idx].store(to_commit, std::memory_order_relaxed);

      // Zero out expired slots. Cap iterations at number of windows.
      idx = (idx + 1) % windows;
      uint32_t cleared = 0;
      while (elapsed > slot_duration_seconds_ && cleared < windows) {
        slots_[idx].store(0, std::memory_order_relaxed);
        elapsed -= slot_duration_seconds_;
        idx = (idx + 1) % windows;
        ++cleared;
      }

      generation_index_.store(idx, std::memory_order_relaxed);
    }

    Runtime::Loader& runtime_;
    // The max_retry resource is nested within the budget to maintain state if the retry budget is
    // toggled.
    ManagedResourceImpl max_retry_resource_;
    const absl::optional<double> budget_percent_;
    const absl::optional<std::chrono::milliseconds> budget_interval_;
    const absl::optional<uint32_t> min_retry_concurrency_;
    const std::string budget_percent_key_;
    const std::string budget_interval_key_;
    const std::string min_retry_concurrency_key_;
    const ResourceLimit& requests_;
    const ResourceLimit& pending_requests_;
    Stats::Gauge& remaining_;
    TimeSource& time_source_;

    // budget_interval is divided into a fixed number of windows.
    // Each window tracks the number of requests that started in that window.
    // Inspired by:
    // https://github.com/tower-rs/tower/blob/master/tower/src/retry/budget/tps_budget.rs
    static constexpr uint32_t windows = 10;
    std::array<std::atomic<uint64_t>, windows> slots_;
    const double slot_duration_seconds_;
    std::atomic<uint64_t> writer_;
    std::atomic<double> generation_time_seconds_;
    std::atomic<uint32_t> generation_index_;
  };

  ManagedResourceImpl connections_;
  ManagedResourceImpl pending_requests_;
  ManagedResourceImpl requests_;
  ManagedResourceImpl connection_pools_;
  uint64_t max_connections_per_host_;
  RetryBudgetImpl retries_;
};

using ResourceManagerImplPtr = std::unique_ptr<ResourceManagerImpl>;

} // namespace Upstream
} // namespace Envoy
