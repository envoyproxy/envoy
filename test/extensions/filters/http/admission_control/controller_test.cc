#include <chrono>

#include "envoy/extensions/filters/http/admission_control/v3/admission_control.pb.h"
#include "envoy/extensions/filters/http/admission_control/v3/admission_control.pb.validate.h"

#include "source/extensions/filters/http/admission_control/thread_local_controller.h"

#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {
namespace {

using RequestData = ThreadLocalController::RequestData;

class ThreadLocalControllerTest : public testing::Test {
public:
  ThreadLocalControllerTest() : window_(5), tlc_(time_system_, window_) {}

protected:
  // Submit a single request per entry in the historical data (this comes out to a single request
  // each second). The final sample does not advance time to allow for testing of this transition.
  void fillHistorySlots(const bool successes = true) {
    std::function<void()> record;
    if (successes) {
      record = [this]() { tlc_.recordSuccess(); };
    } else {
      record = [this]() { tlc_.recordFailure(); };
    }
    for (int tick = 0; tick < window_.count(); ++tick) {
      record();
      time_system_.advanceTimeWait(std::chrono::seconds(1));
    }
    // Don't sleep after the final sample to allow for measurements.
    record();
  }

  Event::SimulatedTimeSystem time_system_;
  std::chrono::seconds window_;
  ThreadLocalControllerImpl tlc_;
};

// Test the basic functionality of the admission controller.
TEST_F(ThreadLocalControllerTest, BasicRecord) {
  EXPECT_EQ(RequestData(0, 0), tlc_.requestCounts());

  tlc_.recordFailure();
  EXPECT_EQ(RequestData(1, 0), tlc_.requestCounts());

  tlc_.recordSuccess();
  EXPECT_EQ(RequestData(2, 1), tlc_.requestCounts());
}

// Verify that stale historical samples are removed when they grow stale.
TEST_F(ThreadLocalControllerTest, RemoveStaleSamples) {
  fillHistorySlots();

  // We expect a single request counted in each second of the window.
  EXPECT_EQ(RequestData(window_.count(), window_.count()), tlc_.requestCounts());

  time_system_.advanceTimeWait(std::chrono::seconds(1));

  // Continuing to sample requests at 1 per second should maintain the same request counts. We'll
  // record failures here.
  fillHistorySlots(false);
  EXPECT_EQ(RequestData(window_.count(), 0), tlc_.requestCounts());

  // Expect the oldest entry to go stale.
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  EXPECT_EQ(RequestData(window_.count() - 1, 0), tlc_.requestCounts());
}

// Verify that stale historical samples are removed when they grow stale.
TEST_F(ThreadLocalControllerTest, RemoveStaleSamples2) {
  fillHistorySlots();

  // We expect a single request counted in each second of the window.
  EXPECT_EQ(RequestData(window_.count(), window_.count()), tlc_.requestCounts());

  // Let's just sit here for a full day. We expect all samples to become stale.
  time_system_.advanceTimeWait(std::chrono::hours(24));

  EXPECT_EQ(RequestData(0, 0), tlc_.requestCounts());
}

// Verify that historical samples are made only when there is data to record.
TEST_F(ThreadLocalControllerTest, VerifyMemoryUsage) {
  // Make sure we don't add any null data to the history if there are sparse requests.
  tlc_.recordSuccess();
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  tlc_.recordSuccess();
  time_system_.advanceTimeWait(std::chrono::seconds(3));
  tlc_.recordSuccess();
  EXPECT_EQ(RequestData(3, 3), tlc_.requestCounts());
}

// Test for function: averageRps.
TEST_F(ThreadLocalControllerTest, AverageRps) {
  // Validate that historical_data_ is empty.
  EXPECT_EQ(0, tlc_.averageRps());

  // Validate that global_data_.requests equals to zero.
  tlc_.requestCounts();
  EXPECT_EQ(0, tlc_.averageRps());

  // Validate the value in one second.
  tlc_.recordSuccess();
  tlc_.recordFailure();
  EXPECT_EQ(2, tlc_.averageRps());

  // Validate that the sampling window is not full.
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  tlc_.recordSuccess();
  EXPECT_EQ(3, tlc_.averageRps());

  // Now clean up the sampling window to validate that the sampling window is full.
  time_system_.advanceTimeWait(std::chrono::seconds(10));
  for (int tick = 0; tick < window_.count(); ++tick) {
    // 1 + 3 + 5 + 7 + 9
    for (int record_count = 0; record_count <= tick * 2; ++record_count) {
      tlc_.recordSuccess();
    }
    time_system_.advanceTimeWait(std::chrono::seconds(1));
  }
  EXPECT_EQ(5, tlc_.averageRps());
}

} // namespace
} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
