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

// Validates that if no failover is set, then all actions are essentially a pass
// through.
class GrpcMuxFailoverNoFailoverTest : public testing::Test {
protected:
  using RequestType = envoy::service::discovery::v3::DiscoveryRequest;
  using ResponseType = envoy::service::discovery::v3::DiscoveryResponse;

  GrpcMuxFailoverNoFailoverTest()
      // The GrpcMuxFailover test uses a the GrpcMuxFailover with mocked GrpcStream objects.
      : primary_stream_owner_(std::make_unique<MockGrpcStream<RequestType, ResponseType>>()),
        primary_stream_(*primary_stream_owner_),
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

// Validates that when establishing a stream, its the stream to the primary service
// that is established.
TEST_F(GrpcMuxFailoverNoFailoverTest, EstablishPrimaryStream) {
  EXPECT_CALL(primary_stream_, establishNewStream());
  grpc_mux_failover_.establishNewStream();
}

// Validates that grpcStreamAvailable forwards to the primary by default.
TEST_F(GrpcMuxFailoverNoFailoverTest, PrimaryStreamAvailableDefault) {
  EXPECT_CALL(primary_stream_, grpcStreamAvailable()).WillOnce(Return(false));
  EXPECT_FALSE(grpc_mux_failover_.grpcStreamAvailable());
}

// Validates that a message is sent to the primary stream by default.
TEST_F(GrpcMuxFailoverNoFailoverTest, SendMessagePrimaryDefault) {
  // Send a message, expect it to be sent to the primary.
  RequestType msg;
  msg.set_version_info("123");
  EXPECT_CALL(primary_stream_, sendMessage(ProtoEq(msg)));
  grpc_mux_failover_.sendMessage(msg);
}

// Validates that updating the queue size of the primary stream by default.
TEST_F(GrpcMuxFailoverNoFailoverTest, MaybeUpdateQueueSizePrimaryDefault) {
  EXPECT_CALL(primary_stream_, maybeUpdateQueueSizeStat(123));
  grpc_mux_failover_.maybeUpdateQueueSizeStat(123);
}

// Validates that checkRateLimitAllowsDrain is invoked on the primary stream
// by default.
TEST_F(GrpcMuxFailoverNoFailoverTest, CheckRateLimitPrimaryStreamDefault) {
  EXPECT_CALL(primary_stream_, checkRateLimitAllowsDrain()).WillOnce(Return(false));
  EXPECT_FALSE(grpc_mux_failover_.checkRateLimitAllowsDrain());
}

// Validates that onStreamEstablished callback is invoked on the primary stream
// callbacks.
TEST_F(GrpcMuxFailoverNoFailoverTest, PrimaryOnStreamEstablishedInvoked) {
  // Establish a stream to the primary source.
  EXPECT_CALL(primary_stream_, establishNewStream());
  grpc_mux_failover_.establishNewStream();

  EXPECT_CALL(grpc_mux_callbacks_, onStreamEstablished());
  primary_callbacks_->onStreamEstablished();
}

// Validates that onEstablishmentFailure callback is invoked on the primary stream
// callbacks.
TEST_F(GrpcMuxFailoverNoFailoverTest, PrimaryOnEstablishmentFailureInvoked) {
  // Establish a stream to the primary source.
  EXPECT_CALL(primary_stream_, establishNewStream());
  grpc_mux_failover_.establishNewStream();

  EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure());
  primary_callbacks_->onEstablishmentFailure();
}

// Validates that onDiscoveryResponse callback is invoked on the primary stream
// callbacks.
TEST_F(GrpcMuxFailoverNoFailoverTest, PrimaryOnDiscoveryResponseInvoked) {
  // Establish a stream to the primary source.
  EXPECT_CALL(primary_stream_, establishNewStream());
  grpc_mux_failover_.establishNewStream();

  std::unique_ptr<ResponseType> response(std::make_unique<ResponseType>());
  response->set_version_info("456");
  Stats::TestUtil::TestStore stats;
  ControlPlaneStats cp_stats{Utility::generateControlPlaneStats(*stats.rootScope())};
  EXPECT_CALL(grpc_mux_callbacks_, onDiscoveryResponse(_, _));
  primary_callbacks_->onDiscoveryResponse(std::move(response), cp_stats);
}

// Validates that onWritable callback is invoked on the primary stream
// callbacks.
TEST_F(GrpcMuxFailoverNoFailoverTest, PrimaryOnWriteableInvoked) {
  // Establish a stream to the primary source, and ensure availabilty (via a
  // response).
  EXPECT_CALL(primary_stream_, establishNewStream());
  grpc_mux_failover_.establishNewStream();
  std::unique_ptr<ResponseType> response(std::make_unique<ResponseType>());
  response->set_version_info("456");
  Stats::TestUtil::TestStore stats;
  ControlPlaneStats cp_stats{Utility::generateControlPlaneStats(*stats.rootScope())};
  EXPECT_CALL(grpc_mux_callbacks_, onDiscoveryResponse(_, _));
  primary_callbacks_->onDiscoveryResponse(std::move(response), cp_stats);

  EXPECT_CALL(grpc_mux_callbacks_, onWriteable());
  primary_callbacks_->onWriteable();
}

// Validates that primary and failover sources are accessed properly.
class GrpcMuxFailoverTest : public testing::Test {
protected:
  using RequestType = envoy::service::discovery::v3::DiscoveryRequest;
  using ResponseType = envoy::service::discovery::v3::DiscoveryResponse;

  GrpcMuxFailoverTest()
      // The GrpcMuxFailover test uses a the GrpcMuxFailover with mocked GrpcStream objects.
      : primary_stream_owner_(std::make_unique<MockGrpcStream<RequestType, ResponseType>>()),
        failover_stream_owner_(std::make_unique<MockGrpcStream<RequestType, ResponseType>>()),
        primary_stream_(*primary_stream_owner_), failover_stream_(*failover_stream_owner_),
        grpc_mux_failover_(
            /*primary_stream_creator=*/
            [this](GrpcStreamCallbacks<ResponseType>* callbacks)
                -> GrpcStreamInterfacePtr<RequestType, ResponseType> {
              primary_callbacks_ = callbacks;
              return std::move(primary_stream_owner_);
            },
            /*failover_stream_creator=*/
            [this](GrpcStreamCallbacks<ResponseType>* callbacks)
                -> GrpcStreamInterfacePtr<RequestType, ResponseType> {
              failover_callbacks_ = callbacks;
              return std::move(failover_stream_owner_);
            },
            /*grpc_mux_callbacks=*/grpc_mux_callbacks_) {}

  std::unique_ptr<MockGrpcStream<RequestType, ResponseType>> primary_stream_owner_;
  std::unique_ptr<MockGrpcStream<RequestType, ResponseType>> failover_stream_owner_;
  MockGrpcStream<RequestType, ResponseType>& primary_stream_;
  MockGrpcStream<RequestType, ResponseType>& failover_stream_;
  NiceMock<MockGrpcStreamCallbacks> grpc_mux_callbacks_;
  GrpcStreamCallbacks<ResponseType>* primary_callbacks_{nullptr};
  GrpcStreamCallbacks<ResponseType>* failover_callbacks_{nullptr};
  GrpcMuxFailover<RequestType, ResponseType> grpc_mux_failover_;
};

// Validates that when establishing a stream, its the stream to the primary service
// that is established, and not the failover.
TEST_F(GrpcMuxFailoverTest, EstablishPrimaryStream) {
  EXPECT_CALL(primary_stream_, establishNewStream());
  EXPECT_CALL(failover_stream_, establishNewStream()).Times(0);
  grpc_mux_failover_.establishNewStream();
}

// Validates that grpcStreamAvailable forwards to the primary by default.
TEST_F(GrpcMuxFailoverTest, PrimaryStreamAvailableDefault) {
  EXPECT_CALL(primary_stream_, grpcStreamAvailable()).WillOnce(Return(false));
  EXPECT_CALL(failover_stream_, grpcStreamAvailable()).Times(0);
  EXPECT_FALSE(grpc_mux_failover_.grpcStreamAvailable());
}

// Validates that grpcStreamAvailable is invoked on the primary stream when connecting to primary.
TEST_F(GrpcMuxFailoverTest, PrimaryStreamAvailableConnectingToPrimary) {
  EXPECT_CALL(primary_stream_, establishNewStream());
  grpc_mux_failover_.establishNewStream();
  EXPECT_CALL(primary_stream_, grpcStreamAvailable()).WillOnce(Return(true));
  EXPECT_CALL(failover_stream_, grpcStreamAvailable()).Times(0);
  EXPECT_TRUE(grpc_mux_failover_.grpcStreamAvailable());
}

// Validates that a message is sent to the primary stream by default.
TEST_F(GrpcMuxFailoverTest, SendMessagePrimaryDefault) {
  // Send a message, expect it to be sent to the primary.
  RequestType msg;
  msg.set_version_info("123");
  EXPECT_CALL(primary_stream_, sendMessage(ProtoEq(msg)));
  EXPECT_CALL(failover_stream_, sendMessage(_)).Times(0);
  grpc_mux_failover_.sendMessage(msg);
}

// Validates that updating the queue size of the primary stream by default.
TEST_F(GrpcMuxFailoverTest, MaybeUpdateQueueSizePrimaryDefault) {
  EXPECT_CALL(primary_stream_, maybeUpdateQueueSizeStat(123));
  EXPECT_CALL(failover_stream_, maybeUpdateQueueSizeStat(123)).Times(0);
  grpc_mux_failover_.maybeUpdateQueueSizeStat(123);
}

// Validates that checkRateLimitAllowsDrain is invoked on the primary stream
// by default.
TEST_F(GrpcMuxFailoverTest, CheckRateLimitPrimaryStreamDefault) {
  EXPECT_CALL(primary_stream_, checkRateLimitAllowsDrain()).WillOnce(Return(false));
  EXPECT_CALL(failover_stream_, checkRateLimitAllowsDrain()).Times(0);
  EXPECT_FALSE(grpc_mux_failover_.checkRateLimitAllowsDrain());
}

// Validate that upon failure of first connection to the primary, the second
// will still be to the primary.
TEST_F(GrpcMuxFailoverTest, AttemptPrimaryAfterPrimaryInitialFailure) {
  // Initial connection attempt.
  EXPECT_CALL(primary_stream_, establishNewStream());
  EXPECT_CALL(failover_stream_, establishNewStream()).Times(0);
  grpc_mux_failover_.establishNewStream();

  // First disconnect.
  EXPECT_CALL(primary_stream_, closeStream()).Times(0);
  EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure());
  EXPECT_CALL(failover_stream_, establishNewStream()).Times(0);
  primary_callbacks_->onEstablishmentFailure();
}

// Validate that upon failure of the second connection to the primary, the
// failover will be attempted.
TEST_F(GrpcMuxFailoverTest, AttemptFailoverAfterPrimaryTwoFailures) {
  // Initial connection attempt.
  EXPECT_CALL(primary_stream_, establishNewStream());
  EXPECT_CALL(failover_stream_, establishNewStream()).Times(0);
  grpc_mux_failover_.establishNewStream();

  // First disconnect.
  EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure());
  primary_callbacks_->onEstablishmentFailure();

  // Emulate a retry that ends with a second disconnect. It should close the
  // primary stream and try to establish the failover stream.
  EXPECT_CALL(primary_stream_, closeStream());
  EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure());
  EXPECT_CALL(primary_stream_, establishNewStream()).Times(0);
  EXPECT_CALL(failover_stream_, establishNewStream());
  primary_callbacks_->onEstablishmentFailure();
}

// Validate that starting from the second failure to reach the primary,
// the attempts will alternate between the failover and the primary.
TEST_F(GrpcMuxFailoverTest, AlternatingBetweenFailoverAndPrimary) {
  // Initial connection attempt.
  EXPECT_CALL(primary_stream_, establishNewStream());
  EXPECT_CALL(failover_stream_, establishNewStream()).Times(0);
  grpc_mux_failover_.establishNewStream();

  // First disconnect.
  EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure());
  primary_callbacks_->onEstablishmentFailure();

  // Emulate a 5 times disconnects.
  for (int attempt = 0; attempt < 5; ++attempt) {
    if (attempt % 2 == 0) {
      // Emulate a primary source failure that will result in an attempt to
      // connect to the failover. It should close the primary stream, and
      // try to establish the failover stream.
      EXPECT_CALL(primary_stream_, closeStream());
      EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure());
      EXPECT_CALL(primary_stream_, establishNewStream()).Times(0);
      EXPECT_CALL(failover_stream_, establishNewStream());
      primary_callbacks_->onEstablishmentFailure();
    } else {
      // Emulate a failover source failure that will result in an attempt to
      // connect to the primary. It should close the failover stream, and
      // try to establish the priamry stream.
      EXPECT_CALL(failover_stream_, closeStream());
      EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure());
      EXPECT_CALL(failover_stream_, establishNewStream()).Times(0);
      EXPECT_CALL(primary_stream_, establishNewStream());
      failover_callbacks_->onEstablishmentFailure();
    }
  }
}

// Validate that after the primary is available (a response is recevied), all
// reconnect attempts will be to the primary.
TEST_F(GrpcMuxFailoverTest, PrimaryOnlyAttemptsAfterPrimaryAvailable) {
  // Initial connection attempt.
  EXPECT_CALL(primary_stream_, establishNewStream());
  EXPECT_CALL(failover_stream_, establishNewStream()).Times(0);
  grpc_mux_failover_.establishNewStream();

  // Connect to the primary and receive a response.
  EXPECT_CALL(grpc_mux_callbacks_, onStreamEstablished());
  primary_callbacks_->onStreamEstablished();
  std::unique_ptr<ResponseType> response(std::make_unique<ResponseType>());
  response->set_version_info("456");
  Stats::TestUtil::TestStore stats;
  ControlPlaneStats cp_stats{Utility::generateControlPlaneStats(*stats.rootScope())};
  EXPECT_CALL(grpc_mux_callbacks_, onDiscoveryResponse(_, _));
  primary_callbacks_->onDiscoveryResponse(std::move(response), cp_stats);

  // Emulate 5 disconnects, and ensure the failover connection isn't attempted.
  for (int attempt = 0; attempt < 5; ++attempt) {
    // Emulate a primary source failure that will not result in an attempt to
    // connect to the failover. It should not close the primary stream (so
    // the retry mechanism will kick in).
    EXPECT_CALL(primary_stream_, closeStream()).Times(0);
    EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure());
    EXPECT_CALL(failover_stream_, establishNewStream()).Times(0);
    primary_callbacks_->onEstablishmentFailure();
  }
}

// Validate that after the failover is available (a response is recevied), all
// reconnect attempts will be to the failover.
TEST_F(GrpcMuxFailoverTest, FailoverOnlyAttemptsAfterFailoverAvailable) {
  // Initial connection attempt.
  EXPECT_CALL(primary_stream_, establishNewStream());
  EXPECT_CALL(failover_stream_, establishNewStream()).Times(0);
  grpc_mux_failover_.establishNewStream();

  // First disconnect.
  EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure());
  primary_callbacks_->onEstablishmentFailure();

  // Emulate a retry that ends with a second disconnect. It should close the
  // primary stream and try to establish the failover stream.
  EXPECT_CALL(primary_stream_, closeStream());
  EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure());
  EXPECT_CALL(primary_stream_, establishNewStream()).Times(0);
  EXPECT_CALL(failover_stream_, establishNewStream());
  primary_callbacks_->onEstablishmentFailure();

  // Connect to the primary and receive a response.
  EXPECT_CALL(grpc_mux_callbacks_, onStreamEstablished());
  failover_callbacks_->onStreamEstablished();
  std::unique_ptr<ResponseType> response(std::make_unique<ResponseType>());
  response->set_version_info("456");
  Stats::TestUtil::TestStore stats;
  ControlPlaneStats cp_stats{Utility::generateControlPlaneStats(*stats.rootScope())};
  EXPECT_CALL(grpc_mux_callbacks_, onDiscoveryResponse(_, _));
  failover_callbacks_->onDiscoveryResponse(std::move(response), cp_stats);

  // Emulate 5 disconnects, and ensure the primary reconnection isn't attempted.
  for (int attempt = 0; attempt < 5; ++attempt) {
    // Emulate a failover source failure that will not result in an attempt to
    // connect to the primary. It should not close the failover stream (so
    // the retry mechanism will kick in).
    EXPECT_CALL(failover_stream_, closeStream()).Times(0);
    EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure());
    EXPECT_CALL(primary_stream_, establishNewStream()).Times(0);
    failover_callbacks_->onEstablishmentFailure();
  }
}

} // namespace
} // namespace Config
} // namespace Envoy
