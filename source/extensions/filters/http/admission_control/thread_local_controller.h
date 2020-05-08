#pragma once

#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/http/codes.h"
#include "envoy/thread_local/thread_local.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {

/*
 * Thread-local admission controller interface.
 */
class ThreadLocalController {
public:
  virtual ~ThreadLocalController() = default;

  // Record success/failure of a request and update the internal state of the controller to reflect
  // this.
  virtual void recordSuccess() PURE;
  virtual void recordFailure() PURE;

  // Returns the current number of recorded requests.
  virtual uint32_t requestTotalCount() PURE;

  // Returns the current number of recorded request successes.
  virtual uint32_t requestSuccessCount() PURE;
};

/**
 * Thread-local object to track request counts and successes over a rolling time window. Request
 * data for the time window is kept recent via a circular buffer that phases out old request/success
 * counts when recording new samples.
 *
 * The look-back window for request samples is accurate up to a hard-coded 1-second granularity.
 * TODO (tonya11en): Allow the granularity to be configurable.
 */
class ThreadLocalControllerImpl : public ThreadLocalController,
                                  public ThreadLocal::ThreadLocalObject {
public:
  ThreadLocalControllerImpl(TimeSource& time_source, std::chrono::seconds sampling_window);
  ~ThreadLocalControllerImpl() override = default;
  void recordSuccess() override { recordRequest(true); }
  void recordFailure() override { recordRequest(false); }

  uint32_t requestTotalCount() override {
    maybeUpdateHistoricalData();
    return global_data_.requests;
  }
  uint32_t requestSuccessCount() override {
    maybeUpdateHistoricalData();
    return global_data_.successes;
  }

private:
  struct RequestData {
    uint32_t requests{0};
    uint32_t successes{0};
  };

  void recordRequest(const bool success);

  // Potentially remove any stale samples and record sample aggregates to the historical data.
  void maybeUpdateHistoricalData();

  // Returns the age of the oldest sample in the historical data.
  std::chrono::microseconds ageOfOldestSample() const {
    ASSERT(!historical_data_.empty());
    using namespace std::chrono;
    return duration_cast<microseconds>(time_source_.monotonicTime() -
                                       historical_data_.front().first);
  }

  // Returns the age of the newest sample in the historical data.
  std::chrono::microseconds ageOfNewestSample() const {
    ASSERT(!historical_data_.empty());
    using namespace std::chrono;
    return duration_cast<microseconds>(time_source_.monotonicTime() -
                                       historical_data_.back().first);
  }

  // Removes the oldest sample in the historical data and reconciles the global data.
  void removeOldestSample() {
    ASSERT(!historical_data_.empty());
    global_data_.successes -= historical_data_.front().second.successes;
    global_data_.requests -= historical_data_.front().second.requests;
    historical_data_.pop_front();
  }

  TimeSource& time_source_;
  std::deque<std::pair<MonotonicTime, RequestData>> historical_data_;

  // Request data aggregated for the whole look-back window.
  RequestData global_data_;

  // The rolling time window size.
  std::chrono::seconds sampling_window_;
};

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
