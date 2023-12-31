#pragma once

#include <cstdint>
#include <memory>

#include "envoy/upstream/admission_control.h"

#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace AdmissionControl {

class ConcurrencyBudget : public Upstream::RetryAdmissionController {
public:
  ConcurrencyBudget(uint64_t min_retry_concurrency_limit, double budget_percent,
                    Runtime::Loader& runtime, std::string budget_percent_key,
                    std::string min_retry_concurrency_limit_key,
                    Upstream::ClusterCircuitBreakersStats cb_stats)
      : cb_stats_(cb_stats), runtime_(runtime), budget_percent_key_(budget_percent_key),
        min_retry_concurrency_limit_key_(min_retry_concurrency_limit_key),
        min_retry_concurrency_limit_(min_retry_concurrency_limit), budget_percent_(budget_percent) {
    if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.use_retry_admission_control")) {
      return;
    }
    uint64_t retries_remaining = runtime_.snapshot().getInteger(min_retry_concurrency_limit_key_,
                                                                min_retry_concurrency_limit_);
    cb_stats_.remaining_retries_.set(retries_remaining);
    cb_stats_.rq_retry_open_.set(retries_remaining > 0 ? 0 : 1);
  };
  ~ConcurrencyBudget() override = default;

  Upstream::RetryStreamAdmissionControllerPtr
  createStreamAdmissionController(const StreamInfo::StreamInfo&) override {
    return std::make_unique<StreamAdmissionController>(
        active_tries_, active_retries_, min_retry_concurrency_limit_, budget_percent_, runtime_,
        budget_percent_key_, min_retry_concurrency_limit_key_, cb_stats_);
  };

private:
  using PrimitiveGaugeSharedPtr = std::shared_ptr<Stats::PrimitiveGauge>;

  class StreamAdmissionController : public Upstream::RetryStreamAdmissionController {
  public:
    StreamAdmissionController(PrimitiveGaugeSharedPtr active_tries,
                              PrimitiveGaugeSharedPtr active_retries,
                              uint64_t min_retry_concurrency_limit, double budget_percent,
                              Runtime::Loader& runtime, std::string budget_percent_key,
                              std::string min_retry_concurrency_limit_key,
                              Upstream::ClusterCircuitBreakersStats cb_stats)
        : cb_stats_(cb_stats), runtime_(runtime), active_tries_(active_tries),
          active_retries_(active_retries), budget_percent_key_(budget_percent_key),
          min_retry_concurrency_limit_key_(min_retry_concurrency_limit_key),
          min_retry_concurrency_limit_(min_retry_concurrency_limit),
          budget_percent_(budget_percent){};

    ~StreamAdmissionController() override {
      active_retries_->sub(stream_active_retry_attempt_numbers_.size());
      active_tries_->sub(stream_active_tries_);
    };

    void onTryStarted(uint64_t) override;
    void onTrySucceeded(uint64_t attempt_number) override;
    void onSuccessfulTryFinished() override;
    void onTryAborted(uint64_t attempt_number) override;
    bool isRetryAdmitted(uint64_t prev_attempt_number, uint64_t retry_attempt_number,
                         bool abort_previous_on_retry) override;

  private:
    uint64_t getRetryConcurrencyLimit() const;
    void setStats();

    Upstream::ClusterCircuitBreakersStats cb_stats_;
    Runtime::Loader& runtime_;
    PrimitiveGaugeSharedPtr active_tries_;
    PrimitiveGaugeSharedPtr active_retries_;
    std::set<uint64_t> stream_active_retry_attempt_numbers_{};
    uint64_t stream_active_tries_{};
    const std::string budget_percent_key_;
    const std::string min_retry_concurrency_limit_key_;
    const uint64_t min_retry_concurrency_limit_;
    const double budget_percent_;
  };

  Upstream::ClusterCircuitBreakersStats cb_stats_;
  Runtime::Loader& runtime_;
  PrimitiveGaugeSharedPtr active_tries_{std::make_shared<Stats::PrimitiveGauge>()};
  PrimitiveGaugeSharedPtr active_retries_{std::make_shared<Stats::PrimitiveGauge>()};
  const std::string budget_percent_key_;
  const std::string min_retry_concurrency_limit_key_;
  const uint64_t min_retry_concurrency_limit_;
  const double budget_percent_;
};

} // namespace AdmissionControl
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
