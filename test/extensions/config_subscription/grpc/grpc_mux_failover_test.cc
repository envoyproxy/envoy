#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/extensions/config_subscription/grpc/grpc_mux_failover.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/extensions/config_subscription/grpc/mocks.h"
#include "test/mocks/config/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Config {
namespace {

class GrpcMuxFailoverTest : public testing::Test {
protected:
  using RequestType = envoy::service::discovery::v3::DiscoveryRequest;
  using ResponseType = envoy::service::discovery::v3::DiscoveryResponse;

  GrpcMuxFailoverTest()
      // The GrpcMuxFailover test uses a the GrpcMuxFailover with mocked GrpcStream objects.
      : primary_stream_owner_(std::make_unique<MockGrpcStream<RequestType, ResponseType>>()),
        primary_stream_(*primary_stream_owner_.get()),
        grpc_mux_failover_(
            /*primary_stream_creator=*/
            [this](GrpcStreamCallbacks<ResponseType>* callbacks)
                -> GrpcStreamInterfacePtr<RequestType, ResponseType> {
              primary_callbacks_ = callbacks;
              return std::move(primary_stream_owner_);
            },
            /*failover_stream_creator=*/absl::nullopt,
            /*grpc_mux_callbacks=*/grpc_mux_callbacks_) {}

  std::unique_ptr<MockGrpcStream<RequestType, ResponseType>> primary_stream_owner_;
  MockGrpcStream<RequestType, ResponseType>& primary_stream_;
  NiceMock<MockGrpcStreamCallbacks> grpc_mux_callbacks_;
  GrpcStreamCallbacks<ResponseType>* primary_callbacks_{nullptr};
  GrpcMuxFailover<RequestType, ResponseType> grpc_mux_failover_;
};

// Validates that when establishing a stream the a stream to the primary service
// is established.
// TODO(adisuissa): Update the test once GrpcMuxFailover no longer just
// passes all calls to the primary stream.
TEST_F(GrpcMuxFailoverTest, EstablishPrimaryStream) {
  EXPECT_CALL(primary_stream_, establishNewStream());
  grpc_mux_failover_.establishNewStream();
}

// Validates that grpcStreamAvailable returns true only if a stream
// is available.
// TODO(adisuissa): Update the test once GrpcMuxFailover no longer just
// passes all calls to the primary stream.
TEST_F(GrpcMuxFailoverTest, ValidatePrimaryStreamAvailable) {
  EXPECT_CALL(primary_stream_, grpcStreamAvailable()).WillOnce(Return(false));
  EXPECT_FALSE(grpc_mux_failover_.grpcStreamAvailable());
}

// Validates that a message is sent to the primary stream once it is available.
// TODO(adisuissa): Update the test once GrpcMuxFailover no longer just
// passes all calls to the primary stream.
TEST_F(GrpcMuxFailoverTest, SendMessagePrimaryAvailable) {
  RequestType msg;
  msg.set_version_info("123");
  EXPECT_CALL(primary_stream_, sendMessage(_));
  grpc_mux_failover_.sendMessage(msg);
}

// Validates that updating the queue size of the primary stream once it is available.
// TODO(adisuissa): Update the test once GrpcMuxFailover no longer just
// passes all calls to the primary stream.
TEST_F(GrpcMuxFailoverTest, MaybeUpdateQueueSizePrimaryAvailable) {
  EXPECT_CALL(primary_stream_, maybeUpdateQueueSizeStat(_));
  grpc_mux_failover_.maybeUpdateQueueSizeStat(123);
}

// Validates that checkRateLimitAllowsDrain is invoked on the primary stream
// once it is available.
// TODO(adisuissa): Update the test once GrpcMuxFailover no longer just
// passes all calls to the primary stream.
TEST_F(GrpcMuxFailoverTest, CheckRateLimitPrimaryStreamAvailable) {
  EXPECT_CALL(primary_stream_, checkRateLimitAllowsDrain()).WillOnce(Return(false));
  EXPECT_FALSE(grpc_mux_failover_.checkRateLimitAllowsDrain());
}

// Validates that onStreamEstablished callback is invoked on the primary stream
// callbacks.
// TODO(adisuissa): Update the test once GrpcMuxFailover no longer just
// passes all calls to the primary stream.
TEST_F(GrpcMuxFailoverTest, PrimaryOnStreamEstablishedInvoked) {
  EXPECT_CALL(grpc_mux_callbacks_, onStreamEstablished());
  primary_callbacks_->onStreamEstablished();
}

// Validates that onEstablishmentFailure callback is invoked on the primary stream
// callbacks.
// TODO(adisuissa): Update the test once GrpcMuxFailover no longer just
// passes all calls to the primary stream.
TEST_F(GrpcMuxFailoverTest, PrimaryOnEstablishmentFailureInvoked) {
  EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure());
  primary_callbacks_->onEstablishmentFailure();
}

// Validates that onDiscoveryResponse callback is invoked on the primary stream
// callbacks.
// TODO(adisuissa): Update the test once GrpcMuxFailover no longer just
// passes all calls to the primary stream.
TEST_F(GrpcMuxFailoverTest, PrimaryOnDiscoveryResponseInvoked) {
  std::unique_ptr<ResponseType> response(std::make_unique<ResponseType>());
  response->set_version_info("456");
  Stats::TestUtil::TestStore stats;
  ControlPlaneStats cp_stats{Utility::generateControlPlaneStats(*stats.rootScope())};
  EXPECT_CALL(grpc_mux_callbacks_, onDiscoveryResponse(_, _));
  primary_callbacks_->onDiscoveryResponse(std::move(response), cp_stats);
}

// Validates that onWritable callback is invoked on the primary stream
// callbacks.
// TODO(adisuissa): Update the test once GrpcMuxFailover no longer just
// passes all calls to the primary stream.
TEST_F(GrpcMuxFailoverTest, PrimaryOnWriteableInvoked) {
  EXPECT_CALL(grpc_mux_callbacks_, onWriteable());
  primary_callbacks_->onWriteable();
}

} // namespace
} // namespace Config
} // namespace Envoy
