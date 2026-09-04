#include <chrono>

#include "envoy/config/overload/v3/overload.pb.h"

#include "source/server/overload_shutdown.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/server/instance.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Return;

namespace Envoy {
namespace Server {
namespace {

constexpr char ShutdownCountStat[] = "overload.envoy.overload_actions.shutdown.shutdown_count";

class OverloadShutdownTest : public testing::Test {
protected:
  envoy::config::overload::v3::ShutdownConfig config_;

  void expectConfigured() {
    EXPECT_CALL(server_.overload_manager_, getShutdownConfig())
        .WillRepeatedly(Return(std::make_optional(config_)));
  }

  void expectRegistration(bool registered) {
    EXPECT_CALL(server_.overload_manager_, registerForAction(_, _, _))
        .WillOnce(Invoke([this, registered](const std::string& action, Event::Dispatcher&,
                                            OverloadActionCb callback) {
          EXPECT_EQ(OverloadActionNames::get().Shutdown, action);
          action_cb_ = callback;
          return registered;
        }));
  }

  // Creates the mock timer that the next createTimer call on the server dispatcher returns.
  Event::MockTimer* expectTimer() {
    return new testing::NiceMock<Event::MockTimer>(&server_.dispatcher_);
  }

  uint64_t shutdownCount() { return stats_.counter(ShutdownCountStat).value(); }

  Envoy::Stats::TestUtil::TestStore stats_;
  testing::NiceMock<MockInstance> server_;
  OverloadActionCb action_cb_;
};

TEST_F(OverloadShutdownTest, DoesNothingWhenActionIsNotConfigured) {
  EXPECT_CALL(server_.overload_manager_, getShutdownConfig()).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(server_.overload_manager_, registerForAction(_, _, _)).Times(0);
  EXPECT_CALL(server_.dispatcher_, createTimer_(_)).Times(0);

  OverloadShutdown shutdown(server_, server_.overload_manager_, *stats_.rootScope());
}

TEST_F(OverloadShutdownTest, DoesNotCreateTimerWhenRegistrationFails) {
  config_.mutable_saturation_duration()->set_seconds(60);
  expectConfigured();
  expectRegistration(false);
  EXPECT_CALL(server_.dispatcher_, createTimer_(_)).Times(0);

  OverloadShutdown shutdown(server_, server_.overload_manager_, *stats_.rootScope());
}

TEST_F(OverloadShutdownTest, ShutsDownAfterSustainedSaturation) {
  config_.mutable_saturation_duration()->set_seconds(60);
  expectConfigured();
  expectRegistration(true);
  Event::MockTimer* saturation_timer = expectTimer();

  OverloadShutdown shutdown(server_, server_.overload_manager_, *stats_.rootScope());

  EXPECT_CALL(*saturation_timer, enableTimer(std::chrono::milliseconds(60000), _));
  action_cb_(OverloadActionState::saturated());

  EXPECT_CALL(server_, failHealthcheck(true));
  EXPECT_CALL(server_, drainListeners(_));
  EXPECT_CALL(server_, shutdown()).Times(0);
  saturation_timer->invokeCallback();
  EXPECT_EQ(1, shutdownCount());
  EXPECT_EQ(Network::DrainDirection::All, server_.drain_manager_.drain_direction_);

  EXPECT_CALL(server_, shutdown());
  server_.drain_manager_.drain_sequence_completion_();
}

TEST_F(OverloadShutdownTest, RecoveryCancelsPendingShutdown) {
  config_.mutable_saturation_duration()->set_seconds(60);
  expectConfigured();
  expectRegistration(true);
  Event::MockTimer* saturation_timer = expectTimer();

  OverloadShutdown shutdown(server_, server_.overload_manager_, *stats_.rootScope());

  EXPECT_CALL(*saturation_timer, enableTimer(std::chrono::milliseconds(60000), _));
  action_cb_(OverloadActionState::saturated());

  EXPECT_CALL(*saturation_timer, disableTimer());
  EXPECT_CALL(server_, shutdown()).Times(0);
  action_cb_(OverloadActionState::inactive());
  EXPECT_EQ(0, shutdownCount());
}

TEST_F(OverloadShutdownTest, ScalingBelowSaturationDoesNotStartCountdown) {
  config_.mutable_saturation_duration()->set_seconds(60);
  expectConfigured();
  expectRegistration(true);
  Event::MockTimer* saturation_timer = expectTimer();

  OverloadShutdown shutdown(server_, server_.overload_manager_, *stats_.rootScope());

  EXPECT_CALL(*saturation_timer, enableTimer(_, _)).Times(0);
  action_cb_(OverloadActionState(UnitFloat(0.9)));
}

TEST_F(OverloadShutdownTest, RepeatedSaturationDoesNotRestartCountdown) {
  config_.mutable_saturation_duration()->set_seconds(60);
  expectConfigured();
  expectRegistration(true);
  Event::MockTimer* saturation_timer = expectTimer();

  OverloadShutdown shutdown(server_, server_.overload_manager_, *stats_.rootScope());

  EXPECT_CALL(*saturation_timer, enableTimer(std::chrono::milliseconds(60000), _));
  action_cb_(OverloadActionState::saturated());
  action_cb_(OverloadActionState::saturated());
}

TEST_F(OverloadShutdownTest, JitterExtendsCountdown) {
  config_.mutable_saturation_duration()->set_seconds(60);
  config_.mutable_max_jitter()->set_seconds(30);
  expectConfigured();
  expectRegistration(true);
  Event::MockTimer* saturation_timer = expectTimer();

  OverloadShutdown shutdown(server_, server_.overload_manager_, *stats_.rootScope());

  EXPECT_CALL(server_.api_.random_, random()).WillOnce(Return(12345));
  EXPECT_CALL(*saturation_timer, enableTimer(std::chrono::milliseconds(60000 + 12345 % 30001), _));
  action_cb_(OverloadActionState::saturated());
}

TEST_F(OverloadShutdownTest, RecoveryDuringDrainDoesNotCancelShutdown) {
  config_.mutable_saturation_duration()->set_seconds(60);
  expectConfigured();
  expectRegistration(true);
  Event::MockTimer* saturation_timer = expectTimer();

  OverloadShutdown shutdown(server_, server_.overload_manager_, *stats_.rootScope());
  action_cb_(OverloadActionState::saturated());
  saturation_timer->invokeCallback();

  EXPECT_CALL(*saturation_timer, disableTimer()).Times(0);
  action_cb_(OverloadActionState::inactive());

  EXPECT_CALL(server_, shutdown());
  server_.drain_manager_.drain_sequence_completion_();
}

} // namespace
} // namespace Server
} // namespace Envoy
