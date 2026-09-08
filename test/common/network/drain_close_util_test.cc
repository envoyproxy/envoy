#include <chrono>

#include "envoy/config/listener/v3/listener.pb.h"

#include "source/common/network/drain_close_util.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Network {
namespace {

constexpr auto Default = envoy::config::listener::v3::Listener::DEFAULT;
constexpr auto ModifyOnly = envoy::config::listener::v3::Listener::MODIFY_ONLY;

class DrainCloseUtilTest : public testing::Test {
public:
  DrainCloseUtilTest() {
    // MockServerFactoryContext defaults timeSource() to its own time_system_, which
    // SimulatedTimeSystemHelper redirects to the simulated clock installed by this fixture.
    setDrainTime(std::chrono::seconds(100));
    setHealthCheckFailed(false);
  }

  void setDrainTime(std::chrono::seconds drain_time) {
    ON_CALL(context_.options_, drainTime()).WillByDefault(Return(drain_time));
  }
  void setHealthCheckFailed(bool failed) {
    ON_CALL(context_, healthCheckFailed()).WillByDefault(Return(failed));
  }
  // Forces the random generator to return `value`, which the gradual ramp compares against.
  void setRandom(uint64_t value) {
    ON_CALL(context_.api_.random_, random()).WillByDefault(Return(value));
  }

  ConnectionDrainEvent gradualEventStartingNow() {
    return ConnectionDrainEvent{time_system_.monotonicTime(), Server::DrainStrategy::Gradual};
  }

  Event::SimulatedTimeSystem time_system_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

// With no drain notification and a healthy server, nothing drains.
TEST_F(DrainCloseUtilTest, NoEventNoDrain) {
  EXPECT_FALSE(shouldDrainClose(context_, Default, std::nullopt));
  EXPECT_FALSE(shouldDrainClose(context_, ModifyOnly, std::nullopt));
}

// A failing health check drains a DEFAULT listener with no drain notification at all, and is
// reversible because it is polled rather than pushed.
TEST_F(DrainCloseUtilTest, HealthCheckFailedDrainsDefaultListener) {
  setHealthCheckFailed(true);
  EXPECT_TRUE(shouldDrainClose(context_, Default, std::nullopt));

  setHealthCheckFailed(false);
  EXPECT_FALSE(shouldDrainClose(context_, Default, std::nullopt));
}

// A MODIFY_ONLY listener ignores the health check state.
TEST_F(DrainCloseUtilTest, HealthCheckFailedIgnoredForModifyOnlyListener) {
  setHealthCheckFailed(true);
  EXPECT_FALSE(shouldDrainClose(context_, ModifyOnly, std::nullopt));
}

// An immediate strategy drains as soon as the notification is received, without consulting the
// clock, the drain time or the random generator.
TEST_F(DrainCloseUtilTest, ImmediateStrategyDrainsAtOnce) {
  const ConnectionDrainEvent event{time_system_.monotonicTime(), Server::DrainStrategy::Immediate};
  EXPECT_CALL(context_.api_.random_, random()).Times(0);
  EXPECT_TRUE(shouldDrainClose(context_, Default, event));
  EXPECT_TRUE(shouldDrainClose(context_, ModifyOnly, event));
}

// A gradual strategy never drains at the instant it is notified: elapsed is 0, and the ramp
// requires elapsed > random % drain_time.
TEST_F(DrainCloseUtilTest, GradualStrategyDoesNotDrainImmediately) {
  const ConnectionDrainEvent event = gradualEventStartingNow();
  setRandom(0);
  EXPECT_FALSE(shouldDrainClose(context_, Default, event));
}

// Once the drain window has fully elapsed, a gradual strategy always drains.
TEST_F(DrainCloseUtilTest, GradualStrategyDrainsAfterDeadline) {
  const ConnectionDrainEvent event = gradualEventStartingNow();
  time_system_.advanceTimeWait(std::chrono::seconds(100));
  // Even the largest random value cannot prevent the drain past the deadline.
  setRandom(std::numeric_limits<uint64_t>::max());
  EXPECT_TRUE(shouldDrainClose(context_, Default, event));
}

// Mid-window the decision is the ramp: drain iff elapsed > random % drain_time.
TEST_F(DrainCloseUtilTest, GradualStrategyRampsWithElapsedTime) {
  const ConnectionDrainEvent event = gradualEventStartingNow();
  time_system_.advanceTimeWait(std::chrono::seconds(30));

  // random % 100 == 29 < 30 elapsed -> drain.
  setRandom(29);
  EXPECT_TRUE(shouldDrainClose(context_, Default, event));
  // random % 100 == 30, not strictly less than 30 elapsed -> no drain.
  setRandom(30);
  EXPECT_FALSE(shouldDrainClose(context_, Default, event));
  // random % 100 == 70 > 30 elapsed -> no drain.
  setRandom(70);
  EXPECT_FALSE(shouldDrainClose(context_, Default, event));
}

// A zero drain time means drain as soon as the connection is notified.
TEST_F(DrainCloseUtilTest, ZeroDrainTimeDrainsAtOnce) {
  setDrainTime(std::chrono::seconds(0));
  EXPECT_TRUE(shouldDrainClose(context_, Default, gradualEventStartingNow()));
}

// A start time in the future (a non-monotonic clock reading) is clamped to zero elapsed rather
// than underflowing into a huge elapsed value.
TEST_F(DrainCloseUtilTest, StartTimeInTheFutureDoesNotUnderflow) {
  const ConnectionDrainEvent event{time_system_.monotonicTime() + std::chrono::seconds(10),
                                   Server::DrainStrategy::Gradual};
  setRandom(0);
  EXPECT_FALSE(shouldDrainClose(context_, Default, event));
}

// A connection that was accepted by a listener reports that listener's drain type.
TEST_F(DrainCloseUtilTest, ListenerDrainTypeFromConnection) {
  NiceMock<MockConnection> connection;
  auto listener_info = std::make_shared<NiceMock<MockListenerInfo>>();
  ON_CALL(*listener_info, drainType()).WillByDefault(Return(ModifyOnly));
  connection.stream_info_.downstream_connection_info_provider_->setListenerInfo(listener_info);

  EXPECT_EQ(ModifyOnly, listenerDrainType(connection));
}

// A connection that was not accepted by a network listener (an API listener connection, for
// example) has no listener info and falls back to the DEFAULT drain type.
TEST_F(DrainCloseUtilTest, ListenerDrainTypeWithoutListenerInfo) {
  NiceMock<MockConnection> connection;
  ASSERT_FALSE(connection.connectionInfoProvider().listenerInfo().has_value());

  EXPECT_EQ(Default, listenerDrainType(connection));
}

} // namespace
} // namespace Network
} // namespace Envoy
