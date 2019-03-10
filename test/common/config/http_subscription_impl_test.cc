#include <memory>

#include "test/common/config/http_subscription_test_harness.h"

#include "gtest/gtest.h"

using testing::InSequence;

namespace Envoy {
namespace Config {
namespace {

class HttpSubscriptionImplTest : public testing::Test, public HttpSubscriptionTestHarness {};
class HttpSubscriptionImplInitFetchTimeoutTest : public testing::Test,
                                                 public HttpSubscriptionTestHarness {
public:
  HttpSubscriptionImplInitFetchTimeoutTest()
      : HttpSubscriptionTestHarness(std::chrono::milliseconds(1000)) {}
  Event::MockTimer* init_timeout_timer_;
};

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

TEST_F(HttpSubscriptionImplInitFetchTimeoutTest, InitialFetchTimeout) {
  InSequence s;

  init_timeout_timer_ = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*init_timeout_timer_, enableTimer(std::chrono::milliseconds(1000)));
  startSubscription({"cluster0", "cluster1"});
  verifyStats(1, 0, 0, 0, 0);

  EXPECT_CALL(callbacks_, onConfigUpdateFailed(nullptr));
  init_timeout_timer_->callback_();
  verifyStats(1, 0, 0, 0, 0);
}

// Validate that initial fetch timer is disabled on config update
TEST_F(HttpSubscriptionImplInitFetchTimeoutTest, DisableInitTimeoutOnSuccess) {
  InSequence s;

  init_timeout_timer_ = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*init_timeout_timer_, enableTimer(std::chrono::milliseconds(1000)));
  startSubscription({"cluster0", "cluster1"});
  verifyStats(1, 0, 0, 0, 0);

  EXPECT_CALL(*init_timeout_timer_, disableTimer()).Times(1);
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
}

// Validate that initial fetch timer is disabled on config update failed
TEST_F(HttpSubscriptionImplInitFetchTimeoutTest, DisableInitTimeoutOnFail) {
  InSequence s;

  init_timeout_timer_ = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*init_timeout_timer_, enableTimer(std::chrono::milliseconds(1000)));
  startSubscription({"cluster0", "cluster1"});
  verifyStats(1, 0, 0, 0, 0);

  EXPECT_CALL(*init_timeout_timer_, disableTimer()).Times(1);
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", false);
}

} // namespace
} // namespace Config
} // namespace Envoy
