#include <memory>

#include "extensions/quic_listeners/quiche/platform/envoy_quic_clock.h"

#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Quic {

TEST(EnvoyQuicClockTest, TestNow) {
  Event::SimulatedTimeSystemHelper time_system;
  Api::ApiPtr api = Api::createApiForTest(time_system);
  Event::DispatcherPtr dispatcher = api->allocateDispatcher();
  EnvoyQuicClock clock(*dispatcher);
  uint64_t mono_time = std::chrono::duration_cast<std::chrono::microseconds>(
                           time_system.monotonicTime().time_since_epoch())
                           .count();
  uint64_t sys_time = std::chrono::duration_cast<std::chrono::microseconds>(
                          time_system.systemTime().time_since_epoch())
                          .count();
  // Advance time by 1000000us.
  time_system.sleep(std::chrono::microseconds(1000000));
  EXPECT_EQ(mono_time + 1000000, (clock.Now() - quic::QuicTime::Zero()).ToMicroseconds());
  EXPECT_EQ(sys_time + 1000000, clock.WallNow().ToUNIXMicroseconds());

  // Advance time by 10us.
  time_system.sleep(std::chrono::microseconds(10));
  EXPECT_EQ(mono_time + 1000000 + 10, (clock.Now() - quic::QuicTime::Zero()).ToMicroseconds());
  EXPECT_EQ(sys_time + 1000000 + 10, clock.WallNow().ToUNIXMicroseconds());
  EXPECT_EQ(clock.ApproximateNow(), clock.Now());

  // Advance time by 2ms.
  time_system.sleep(std::chrono::milliseconds(2));
  EXPECT_EQ(mono_time + 1000000 + 10 + 2 * 1000,
            (clock.Now() - quic::QuicTime::Zero()).ToMicroseconds());
  EXPECT_EQ(sys_time + 1000000 + 10 + 2 * 1000, clock.WallNow().ToUNIXMicroseconds());
}

// Tests that Now() should never go back.
TEST(EnvoyQuicClockTest, TestMonotonicityWithReadTimeSystem) {
  Event::TestRealTimeSystem time_system;
  Api::ApiPtr api = Api::createApiForTest(time_system);
  Event::DispatcherPtr dispatcher = api->allocateDispatcher();
  EnvoyQuicClock clock(*dispatcher);
  quic::QuicTime last_now = clock.Now();
  for (int i = 0; i < 1000; ++i) {
    quic::QuicTime now = clock.Now();
    ASSERT_LE(last_now, now);
    last_now = now;
  }
}

} // namespace Quic
} // namespace Envoy
