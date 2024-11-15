#include "source/extensions/filters/http/admission_control/thread_local_controller.h"

#include <cstdint>

#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/http/codes.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {

static constexpr std::chrono::seconds defaultHistoryGranularity{1};

ThreadLocalControllerImpl::ThreadLocalControllerImpl(TimeSource& time_source,
                                                     std::chrono::seconds sampling_window)
    : time_source_(time_source), sampling_window_(sampling_window) {}

uint32_t ThreadLocalControllerImpl::averageRps() const {
  if (historical_data_.empty() || global_data_.requests == 0) {
    return 0;
  }
  using std::chrono::seconds;
  seconds secs =
      std::max(sampling_window_, std::chrono::duration_cast<seconds>(ageOfOldestSample()));
  return global_data_.requests / secs.count();
}

void ThreadLocalControllerImpl::maybeUpdateHistoricalData() {
  // Purge stale samples.
  while (!historical_data_.empty() && ageOfOldestSample() >= sampling_window_) {
    removeOldestSample();
  }

  // It's possible we purged stale samples from the history and are left with nothing, so it's
  // necessary to add an empty entry. We will also need to roll over into a new entry in the
  // historical data if we've exceeded the time specified by the granularity.
  if (historical_data_.empty() || ageOfNewestSample() >= defaultHistoryGranularity) {
    historical_data_.emplace_back(time_source_.monotonicTime(), RequestData());
  }
}

void ThreadLocalControllerImpl::recordRequest(bool success) {
  maybeUpdateHistoricalData();

  // The back of the deque will be the most recent samples.
  ++historical_data_.back().second.requests;
  ++global_data_.requests;
  if (success) {
    ++historical_data_.back().second.successes;
    ++global_data_.successes;
  }
}

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
