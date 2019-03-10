#include "test/common/config/grpc_subscription_test_harness.h"

#include "gtest/gtest.h"

using testing::InSequence;

namespace Envoy {
namespace Config {
namespace {

class GrpcSubscriptionImplTest : public testing::Test, public GrpcSubscriptionTestHarness {};
class GrpcSubscriptionImplInitFetchTimeoutTest : public testing::Test,
                                                 public GrpcSubscriptionTestHarness {
public:
  GrpcSubscriptionImplInitFetchTimeoutTest()
      : GrpcSubscriptionTestHarness(std::chrono::milliseconds(1000)) {}
  Event::MockTimer* init_timeout_timer_;
};

// Validate that stream creation results in a timer based retry and can recover.
TEST_F(GrpcSubscriptionImplTest, StreamCreationFailure) {
  InSequence s;
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(nullptr));

  EXPECT_CALL(callbacks_, onConfigUpdateFailed(_));
  EXPECT_CALL(random_, random());
  EXPECT_CALL(*timer_, enableTimer(_));
  subscription_->start({"cluster0", "cluster1"}, callbacks_);
  verifyStats(2, 0, 0, 1, 0);
  // Ensure this doesn't cause an issue by sending a request, since we don't
  // have a gRPC stream.
  subscription_->updateResources({"cluster2"});

  // Retry and succeed.
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));

  expectSendMessage({"cluster2"}, "");
  timer_cb_();
  verifyStats(3, 0, 0, 1, 0);
  verifyControlPlaneStats(1);
}

// Validate that the client can recover from a remote stream closure via retry.
TEST_F(GrpcSubscriptionImplTest, RemoteStreamClose) {
  startSubscription({"cluster0", "cluster1"});
  verifyStats(1, 0, 0, 0, 0);
  Http::HeaderMapPtr trailers{new Http::TestHeaderMapImpl{}};
  subscription_->grpcMux().onReceiveTrailingMetadata(std::move(trailers));
  EXPECT_CALL(callbacks_, onConfigUpdateFailed(_));
  EXPECT_CALL(*timer_, enableTimer(_));
  EXPECT_CALL(random_, random());
  subscription_->grpcMux().onRemoteClose(Grpc::Status::GrpcStatus::Canceled, "");
  verifyStats(2, 0, 0, 1, 0);
  verifyControlPlaneStats(0);

  // Retry and succeed.
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({"cluster0", "cluster1"}, "");
  timer_cb_();
  verifyStats(2, 0, 0, 1, 0);
}

// Validate that When the management server gets multiple requests for the same version, it can
// ignore later ones. This allows the nonce to be used.
TEST_F(GrpcSubscriptionImplTest, RepeatedNonce) {
  InSequence s;
  startSubscription({"cluster0", "cluster1"});
  verifyStats(1, 0, 0, 0, 0);
  // First with the initial, empty version update to "0".
  updateResources({"cluster2"});
  verifyStats(2, 0, 0, 0, 0);
  deliverConfigUpdate({"cluster0", "cluster2"}, "0", false);
  verifyStats(3, 0, 1, 0, 0);
  deliverConfigUpdate({"cluster0", "cluster2"}, "0", true);
  verifyStats(4, 1, 1, 0, 7148434200721666028);
  // Now with version "0" update to "1".
  updateResources({"cluster3"});
  verifyStats(5, 1, 1, 0, 7148434200721666028);
  deliverConfigUpdate({"cluster3"}, "1", false);
  verifyStats(6, 1, 2, 0, 7148434200721666028);
  deliverConfigUpdate({"cluster3"}, "1", true);
  verifyStats(7, 2, 2, 0, 13237225503670494420U);
}

// Validate that initial fetch timer is created and calls callback on timeout
TEST_F(GrpcSubscriptionImplInitFetchTimeoutTest, InitialFetchTimeout) {
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
TEST_F(GrpcSubscriptionImplInitFetchTimeoutTest, DisableInitTimeoutOnSuccess) {
  InSequence s;

  init_timeout_timer_ = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*init_timeout_timer_, enableTimer(std::chrono::milliseconds(1000)));
  startSubscription({"cluster0", "cluster1"});
  verifyStats(1, 0, 0, 0, 0);

  EXPECT_CALL(*init_timeout_timer_, disableTimer()).Times(1);
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
}

// Validate that initial fetch timer is disabled on config update failed
TEST_F(GrpcSubscriptionImplInitFetchTimeoutTest, DisableInitTimeoutOnFail) {
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
