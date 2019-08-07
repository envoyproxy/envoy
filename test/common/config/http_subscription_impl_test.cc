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
  EXPECT_CALL(*timer_, enableTimer(_));
  EXPECT_CALL(callbacks_,
              onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure, _));
  http_callbacks_->onFailure(Http::AsyncClient::FailureReason::Reset);
  EXPECT_TRUE(statsAre(1, 0, 0, 1, 0, 0));
  timerTick();
  EXPECT_TRUE(statsAre(2, 0, 0, 1, 0, 0));
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  EXPECT_TRUE(statsAre(3, 1, 0, 1, 0, 7148434200721666028));
}

// Validate that the client can recover from bad JSON responses.
TEST_F(HttpSubscriptionImplTest, BadJsonRecovery) {
  startSubscription({"cluster0", "cluster1"});
  Http::HeaderMapPtr response_headers{new Http::TestHeaderMapImpl{{":status", "200"}}};
  Http::MessagePtr message{new Http::ResponseMessageImpl(std::move(response_headers))};
  message->body() = std::make_unique<Buffer::OwnedImpl>(";!@#badjso n");
  EXPECT_CALL(random_gen_, random()).WillOnce(Return(0));
  EXPECT_CALL(*timer_, enableTimer(_));
  EXPECT_CALL(callbacks_,
              onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure, _));
  http_callbacks_->onSuccess(std::move(message));
  EXPECT_TRUE(statsAre(1, 0, 0, 1, 0, 0));
  request_in_progress_ = false;
  timerTick();
  EXPECT_TRUE(statsAre(2, 0, 0, 1, 0, 0));
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  EXPECT_TRUE(statsAre(3, 1, 0, 1, 0, 7148434200721666028));
}

TEST_F(HttpSubscriptionImplTest, ConfigNotModified) {
  startSubscription({"cluster0", "cluster1"});

  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, 0));
  timerTick();
  EXPECT_TRUE(statsAre(2, 0, 0, 0, 0, 0));

  // accept and modify.
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true, true, "200");
  EXPECT_TRUE(statsAre(3, 1, 0, 0, 0, 7148434200721666028));

  // accept and does not modify.
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true, false, "304");
  EXPECT_TRUE(statsAre(4, 1, 0, 0, 0, 7148434200721666028));
}

} // namespace
} // namespace Config
} // namespace Envoy
