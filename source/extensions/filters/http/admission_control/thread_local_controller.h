#pragma once

#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/http/codes.h"
#include "envoy/thread_local/thread_local_object.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {

/*
 * Thread-local admission controller interface.
 */
class ThreadLocalController {
public:
  struct RequestData {
    RequestData(uint32_t request_count, uint32_t success_count)
        : requests(request_count), successes(success_count) {}
    RequestData() = default;

    inline bool operator==(const RequestData& rhs) const {
      return (requests == rhs.requests) && (successes == rhs.successes);
    }

    uint32_t requests{0};
    uint32_t successes{0};
  };

  virtual ~ThreadLocalController() = default;

  // Record success/failure of a request and update the internal state of the controller to reflect
  // this.
  virtual void recordSuccess() PURE;
  virtual void recordFailure() PURE;

  // Returns the current number of requests and how many of them are successful.
  virtual RequestData requestCounts() PURE;

  // Returns the average RPS across the sampling window.
  virtual uint32_t averageRps() const PURE;

  // Returns the sample window for this controller.
  virtual std::chrono::seconds samplingWindow() const PURE;
};

/**
 * Thread-local object to track request counts and successes over a rolling time window. Request
 * data for the time window is kept recent via a circular buffer that phases out old request/success
 * counts when recording new samples.
 *
 * This controller is thread-local so that we do not need to take any locks on the sample histories
 * to update them, at the cost of decreasing the number of samples.
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

  RequestData requestCounts() override {
    maybeUpdateHistoricalData();
    return global_data_;
  }

  uint32_t averageRps() const override;

  std::chrono::seconds samplingWindow() const override { return sampling_window_; }

private:
  void recordRequest(bool success);

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

  // Stores samples from oldest (front) to newest (back). Since there is no need to read/modify
  // entries that are not the oldest or newest (front/back), we can get away with using a deque
  // which allocates memory in chunks and keeps most elements contiguous and cache-friendly.
  std::deque<std::pair<MonotonicTime, RequestData>> historical_data_;

  // Request data aggregated for the whole look-back window.
  RequestData global_data_;

  // The rolling time window size.
  const std::chrono::seconds sampling_window_;
};

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
