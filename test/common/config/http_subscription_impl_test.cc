#include <memory>

#include "test/common/config/http_subscription_test_harness.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

class HttpSubscriptionImplTest : public testing::Test, public HttpSubscriptionTestHarness {};

// Validate that the client can recover from a remote fetch failure.
TEST_F(HttpSubscriptionImplTest, OnRequestReset) {
  startSubscription({"cluster0", "cluster1"});
  EXPECT_CALL(random_gen_, random()).WillOnce(Return(0));
  EXPECT_CALL(*timer_, enableTimer(_, _));
  EXPECT_CALL(callbacks_,
              onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure, _))
      .Times(0);
  http_callbacks_->onFailure(http_request_, Http::AsyncClient::FailureReason::Reset);
  EXPECT_TRUE(statsAre(1, 0, 0, 1, 0, 0, 0, ""));
  timerTick();
  EXPECT_TRUE(statsAre(2, 0, 0, 1, 0, 0, 0, ""));
  deliverConfigUpdate({"cluster0", "cluster1"}, "42", true);
  EXPECT_TRUE(statsAre(3, 1, 0, 1, 0, TEST_TIME_MILLIS, 7919287270473417401, "42"));
}

// Validate that the client can recover from bad JSON responses.
TEST_F(HttpSubscriptionImplTest, BadJsonRecovery) {
  startSubscription({"cluster0", "cluster1"});
  Http::ResponseHeaderMapPtr response_headers{
      new Http::TestResponseHeaderMapImpl{{":status", "200"}}};
  Http::ResponseMessagePtr message{new Http::ResponseMessageImpl(std::move(response_headers))};
  message->body() = std::make_unique<Buffer::OwnedImpl>(";!@#badjso n");
  EXPECT_CALL(random_gen_, random()).WillOnce(Return(0));
  EXPECT_CALL(*timer_, enableTimer(_, _));
  EXPECT_CALL(callbacks_,
              onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected, _));
  http_callbacks_->onSuccess(http_request_, std::move(message));
  EXPECT_TRUE(statsAre(1, 0, 1, 0, 0, 0, 0, ""));
  request_in_progress_ = false;
  timerTick();
  EXPECT_TRUE(statsAre(2, 0, 1, 0, 0, 0, 0, ""));
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  EXPECT_TRUE(statsAre(3, 1, 1, 0, 0, TEST_TIME_MILLIS, 7148434200721666028, "0"));
}

TEST_F(HttpSubscriptionImplTest, ConfigNotModified) {
  startSubscription({"cluster0", "cluster1"});

  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, 0, 0, ""));
  timerTick();
  EXPECT_TRUE(statsAre(2, 0, 0, 0, 0, 0, 0, ""));

  // accept and modify.
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true, true, "200");
  EXPECT_TRUE(statsAre(3, 1, 0, 0, 0, TEST_TIME_MILLIS, 7148434200721666028, "0"));

  // accept and does not modify.
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true, false, "304");
  EXPECT_TRUE(statsAre(4, 1, 0, 0, 0, TEST_TIME_MILLIS, 7148434200721666028, "0"));
}

TEST_F(HttpSubscriptionImplTest, UpdateTimeNotChangedOnUpdateReject) {
  startSubscription({"cluster0", "cluster1"});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, 0, 0, ""));
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", false);
  EXPECT_TRUE(statsAre(2, 0, 1, 0, 0, 0, 0, ""));
}

TEST_F(HttpSubscriptionImplTest, UpdateTimeChangedOnUpdateSuccess) {
  startSubscription({"cluster0", "cluster1"});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, 0, 0, ""));
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  EXPECT_TRUE(statsAre(2, 1, 0, 0, 0, TEST_TIME_MILLIS, 7148434200721666028, "0"));

  // Advance the simulated time and verify that a trivial update (no change) also changes the update
  // time.
  simTime().setSystemTime(SystemTime(std::chrono::milliseconds(TEST_TIME_MILLIS + 1)));
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  EXPECT_TRUE(statsAre(3, 2, 0, 0, 0, TEST_TIME_MILLIS + 1, 7148434200721666028, "0"));
}

} // namespace
} // namespace Config
} // namespace Envoy
