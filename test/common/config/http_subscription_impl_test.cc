#include <memory>

#include "test/common/config/http_subscription_test_harness.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

class HttpSubscriptionImplTest : public HttpSubscriptionTestHarness, public testing::Test {};

// Validate that the client can recover from a remote fetch failure.
TEST_F(HttpSubscriptionImplTest, OnRequestReset) {
  startSubscription({"cluster0", "cluster1"});
  EXPECT_CALL(random_gen_, random()).WillOnce(Return(0));
  EXPECT_CALL(*timer_, enableTimer(_));
  EXPECT_CALL(callbacks_, onConfigUpdateFailed(_));
  http_callbacks_->onFailure(Http::AsyncClient::FailureReason::Reset);
  verifyStats(1, 0, 0, 1, 0);
  timerTick();
  verifyStats(2, 0, 0, 1, 0);
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  verifyStats(3, 1, 0, 1, 7148434200721666028);
}

// Validate that the client can recover from bad JSON responses.
TEST_F(HttpSubscriptionImplTest, BadJsonRecovery) {
  startSubscription({"cluster0", "cluster1"});
  Http::HeaderMapPtr response_headers{new Http::TestHeaderMapImpl{{":status", "200"}}};
  Http::MessagePtr message{new Http::ResponseMessageImpl(std::move(response_headers))};
  message->body() = std::make_unique<Buffer::OwnedImpl>(";!@#badjso n");
  EXPECT_CALL(random_gen_, random()).WillOnce(Return(0));
  EXPECT_CALL(*timer_, enableTimer(_));
  EXPECT_CALL(callbacks_, onConfigUpdateFailed(_));
  http_callbacks_->onSuccess(std::move(message));
  verifyStats(1, 0, 0, 1, 0);
  request_in_progress_ = false;
  timerTick();
  verifyStats(2, 0, 0, 1, 0);
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  verifyStats(3, 1, 0, 1, 7148434200721666028);
}

} // namespace
} // namespace Config
} // namespace Envoy
