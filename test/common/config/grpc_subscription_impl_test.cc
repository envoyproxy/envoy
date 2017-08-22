#include "test/common/config/grpc_subscription_test_harness.h"

#include "gtest/gtest.h"

using testing::InSequence;

namespace Envoy {
namespace Config {
namespace {

class GrpcSubscriptionImplTest : public GrpcSubscriptionTestHarness, public testing::Test {};

// Validate that stream creation results in a timer based retry and can recover.
TEST_F(GrpcSubscriptionImplTest, StreamCreationFailure) {
  InSequence s;
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(nullptr));
  EXPECT_CALL(callbacks_, onConfigUpdateFailed(_));
  EXPECT_CALL(*timer_, enableTimer(_));
  subscription_->start({"cluster0", "cluster1"}, callbacks_);
  verifyStats(1, 0, 0, 1);
  // Ensure this doesn't cause an issue by sending a request, since we don't
  // have a gRPC stream.
  subscription_->updateResources({"cluster2"});
  // Retry and succeed.
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({"cluster2"}, "");
  timer_cb_();
  verifyStats(2, 0, 0, 1);
}

// Validate that the client can recover from a remote stream closure via retry.
TEST_F(GrpcSubscriptionImplTest, RemoteStreamClose) {
  startSubscription({"cluster0", "cluster1"});
  verifyStats(1, 0, 0, 0);
  Http::HeaderMapPtr trailers{new Http::TestHeaderMapImpl{}};
  subscription_->onReceiveTrailingMetadata(std::move(trailers));
  EXPECT_CALL(*timer_, enableTimer(_));
  EXPECT_CALL(callbacks_, onConfigUpdateFailed(_));
  subscription_->onRemoteClose(Grpc::Status::GrpcStatus::Canceled);
  verifyStats(1, 0, 0, 1);
  // Retry and succeed.
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({"cluster0", "cluster1"}, "");
  timer_cb_();
  verifyStats(2, 0, 0, 1);
}

} // namespace
} // namespace Config
} // namespace Envoy
