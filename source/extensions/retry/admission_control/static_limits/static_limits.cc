#include "static_limits.h"

#include <cstdint>

#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace AdmissionControl {

StaticLimits::StreamAdmissionController::~StreamAdmissionController() {
  active_retries_->sub(stream_active_retry_attempt_numbers_.size());
  setStats(active_retries_->value(), getMaxActiveRetries());
};

void StaticLimits::StreamAdmissionController::onTrySucceeded(uint64_t attempt_number) {
  if (stream_active_retry_attempt_numbers_.erase(attempt_number)) {
    // once a retry reaches the "success" phase, it is no longer considered an active retry
    active_retries_->dec();
    cb_stats_.remaining_retries_.inc();
    setStats(active_retries_->value(), getMaxActiveRetries());
  }
}

void StaticLimits::StreamAdmissionController::onTryAborted(uint64_t attempt_number) {
  if (stream_active_retry_attempt_numbers_.erase(attempt_number)) {
    active_retries_->dec();
    cb_stats_.remaining_retries_.inc();
    setStats(active_retries_->value(), getMaxActiveRetries());
  }
}

bool StaticLimits::StreamAdmissionController::isRetryAdmitted(uint64_t prev_attempt_number,
                                                              uint64_t retry_attempt_number,
                                                              bool abort_previous_on_retry) {
  uint64_t active_retry_diff_on_retry = 1;
  if (abort_previous_on_retry && stream_active_retry_attempt_numbers_.find(prev_attempt_number) !=
                                     stream_active_retry_attempt_numbers_.end()) {
    // if we admit the retry, we will abort the previous try which was a retry,
    // so the total number of active retries will not change
    active_retry_diff_on_retry = 0;
  }
  uint64_t max_active_retries = getMaxActiveRetries();
  uint64_t active_retries = active_retries_->value();
  if (active_retries + active_retry_diff_on_retry > max_active_retries) {
    // odds against, but something _might_ have changed due to runtime overrides
    // so better to be safe and set stats again.
    // this prevents stats from being unrepresentative while the circuit breaker is open.
    setStats(active_retries, max_active_retries);
    return false;
  }
  active_retries_->add(active_retry_diff_on_retry);
  if (abort_previous_on_retry) {
    stream_active_retry_attempt_numbers_.erase(prev_attempt_number);
  }
  stream_active_retry_attempt_numbers_.insert(retry_attempt_number);
  setStats(active_retries + active_retry_diff_on_retry, max_active_retries);
  return true;
};

uint64_t StaticLimits::StreamAdmissionController::getMaxActiveRetries() const {
  return runtime_.snapshot().getInteger(max_active_retries_key_, max_active_retries_);
}

void StaticLimits::StreamAdmissionController::setStats(uint64_t active_retries,
                                                       uint64_t max_active_retries) {
  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.use_retry_admission_control")) {
    return;
  }
  uint64_t retries_remaining = 0;
  if (max_active_retries > active_retries) {
    retries_remaining = max_active_retries - active_retries;
  }
  cb_stats_.remaining_retries_.set(retries_remaining);
  cb_stats_.rq_retry_open_.set(retries_remaining > 0 ? 0 : 1);
}

} // namespace AdmissionControl
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
