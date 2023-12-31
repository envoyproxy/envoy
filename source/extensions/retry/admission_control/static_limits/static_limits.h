#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/upstream/admission_control.h"

#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace AdmissionControl {

class StaticLimits : public Upstream::RetryAdmissionController {
public:
  StaticLimits(uint64_t max_concurrent_retries, Runtime::Loader& runtime,
               std::string max_active_retries_key, Upstream::ClusterCircuitBreakersStats cb_stats)
      : cb_stats_(cb_stats), runtime_(runtime), max_active_retries_(max_concurrent_retries),
        max_active_retries_key_(max_active_retries_key) {
    if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.use_retry_admission_control")) {
      return;
    }
    uint64_t max_active_retries =
        runtime_.snapshot().getInteger(max_active_retries_key_, max_active_retries_);
    cb_stats_.remaining_retries_.set(max_concurrent_retries);
    cb_stats_.rq_retry_open_.set(max_active_retries > 0 ? 0 : 1);
  };

  Upstream::RetryStreamAdmissionControllerPtr
  createStreamAdmissionController(const StreamInfo::StreamInfo&) override {
    return std::make_unique<StreamAdmissionController>(
        active_retries_, max_active_retries_, runtime_, max_active_retries_key_, cb_stats_);
  };

private:
  using PrimitiveGaugeSharedPtr = std::shared_ptr<Stats::PrimitiveGauge>;
  class StreamAdmissionController : public Upstream::RetryStreamAdmissionController {
  public:
    StreamAdmissionController(PrimitiveGaugeSharedPtr active_retries,
                              const uint64_t max_active_retries, Runtime::Loader& runtime,
                              std::string max_active_retries_key,
                              Upstream::ClusterCircuitBreakersStats cb_stats)
        : cb_stats_(cb_stats), runtime_(runtime), active_retries_(active_retries),
          max_active_retries_(max_active_retries),
          max_active_retries_key_(max_active_retries_key){};

    ~StreamAdmissionController() override;

    void onTryStarted(uint64_t) override {}
    void onTrySucceeded(uint64_t attempt_number) override;
    void onSuccessfulTryFinished() override {}
    void onTryAborted(uint64_t attempt_number) override;
    bool isRetryAdmitted(uint64_t prev_attempt_number, uint64_t retry_attempt_number,
                         bool abort_previous_on_retry) override;

  private:
    uint64_t getMaxActiveRetries() const;
    void setStats(uint64_t active_retries, uint64_t max_retries);

    Upstream::ClusterCircuitBreakersStats cb_stats_;
    Runtime::Loader& runtime_;
    PrimitiveGaugeSharedPtr active_retries_;
    std::set<uint64_t> stream_active_retry_attempt_numbers_{};
    const uint64_t max_active_retries_;
    const std::string max_active_retries_key_;
  };

  Upstream::ClusterCircuitBreakersStats cb_stats_;
  Runtime::Loader& runtime_;
  PrimitiveGaugeSharedPtr active_retries_{std::make_shared<Stats::PrimitiveGauge>()};
  const uint64_t max_active_retries_;
  const std::string max_active_retries_key_;
};

} // namespace AdmissionControl
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
