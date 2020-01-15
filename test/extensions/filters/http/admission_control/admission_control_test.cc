#include <chrono>

#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"
#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.validate.h"

#include "common/stats/isolated_store_impl.h"

#include "extensions/filters/http/admission_control/admission_control.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AllOf;
using testing::Ge;
using testing::Le;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {
namespace {

class AdmissionControlConfigTest : public testing::Test {
public:
  AdmissionControlConfigTest() = default;

protected:
  NiceMock<Runtime::MockLoader> runtime_;
  Event::SimulatedTimeSystem time_system_;
};

class AdmissionControlTest : public testing::Test {
public:
  AdmissionControlTest() = default;

protected:
  Event::SimulatedTimeSystem time_system_;
  Stats::IsolatedStoreImpl stats_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
};

class ThreadLocalControllerTest : public testing::Test {
public:
  ThreadLocalControllerTest() : window_(100), tlc_(time_system_, std::chrono::seconds(100)) {}

protected:
  Event::SimulatedTimeSystem time_system_;
  std::chrono::seconds window_;
  ThreadLocalController tlc_;
};

TEST_F(ThreadLocalControllerTest, BasicRecord) {
  EXPECT_EQ(0, tlc_.requestTotalCount());
  EXPECT_EQ(0, tlc_.requestSuccessCount());

  tlc_.recordFailure();
  EXPECT_EQ(1, tlc_.requestTotalCount());
  EXPECT_EQ(0, tlc_.requestSuccessCount());

  tlc_.recordSuccess();
  EXPECT_EQ(2, tlc_.requestTotalCount());
  EXPECT_EQ(1, tlc_.requestSuccessCount());
}

TEST_F(ThreadLocalControllerTest, RemoveStaleSamples) {
  // Per-second stats are aggregated, so let's fill up all of the slots in the internal ring buffer.
  for (int tick = 0; tick < window_.count(); ++tick) {
    tlc_.recordSuccess();
    time_system_.sleep(std::chrono::seconds(1));
  }

  // We expect a single request counted in each second of the window.
  EXPECT_EQ(window_.count(), tlc_.requestTotalCount());
  EXPECT_EQ(window_.count(), tlc_.requestSuccessCount());

  // Continuing to sample requests at 1 per second should maintain the same request counts. We'll
  // record failures here.
  for (int tick = 0; tick < window_.count(); ++tick) {
    tlc_.recordFailure();
    time_system_.sleep(std::chrono::seconds(1));
  }
  EXPECT_EQ(window_.count(), tlc_.requestTotalCount());
  EXPECT_EQ(0, tlc_.requestSuccessCount());
}

} // namespace
} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
