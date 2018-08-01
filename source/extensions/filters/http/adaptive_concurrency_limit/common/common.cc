#include "extensions/filters/http/adaptive_concurrency_limit/common/common.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrencyLimit {
namespace Common {

SampleWindow::SampleWindow() : min_rtt_(std::chrono::nanoseconds(INT64_MAX)) {}

void SampleWindow::addSample(std::chrono::nanoseconds rtt, uint32_t inflight_requests) {
  sum_ += rtt;
  min_rtt_.set(rtt);
  max_inflight_requests_.set(inflight_requests);
  sample_count_++;
}

void SampleWindow::addDroppedSample(uint32_t inflight_requests) {
  did_drop_ = true;
  max_inflight_requests_.set(inflight_requests);
}

} // namespace Common
} // namespace AdaptiveConcurrencyLimit
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy