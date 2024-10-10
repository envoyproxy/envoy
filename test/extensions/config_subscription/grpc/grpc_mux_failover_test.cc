#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/extensions/config_subscription/grpc/grpc_mux_failover.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/extensions/config_subscription/grpc/mocks.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Config {

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
            /*grpc_mux_callbacks=*/grpc_mux_callbacks_,
            /*dispatcher=*/dispatcher_) {}

  NiceMock<Event::MockDispatcher> dispatcher_;
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

  EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure(false));
  primary_callbacks_->onEstablishmentFailure(false);
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
  // Establish a stream to the primary source, and ensure availability (via a
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
      : timer_(new Event::MockTimer()),
        primary_stream_owner_(std::make_unique<MockGrpcStream<RequestType, ResponseType>>()),
        failover_stream_owner_(std::make_unique<MockGrpcStream<RequestType, ResponseType>>()),
        primary_stream_(*primary_stream_owner_), failover_stream_(*failover_stream_owner_) {
    // Overwrite the timer and keep the callback to emulate its invocations later.
    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([this](Event::TimerCb timer_cb) {
      timer_cb_ = timer_cb;
      return timer_;
    }));
    grpc_mux_failover_ = std::make_unique<GrpcMuxFailover<RequestType, ResponseType>>(
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
        /*grpc_mux_callbacks=*/grpc_mux_callbacks_,
        /*dispatcher=*/dispatcher_);
    EXPECT_CALL(*timer_, disableTimer()).Times(testing::AnyNumber());
  }

  // Get to a connecting to primary state.
  // Attempts to establish a stream to the primary source.
  void connectingToPrimary() {
    // Initial connection attempt.
    EXPECT_CALL(primary_stream_, establishNewStream());
    EXPECT_CALL(failover_stream_, establishNewStream()).Times(0);
    grpc_mux_failover_->establishNewStream();
  }

  // Successfully connect to the primary source.
  // Attempts to establish a stream to the primary source, and receives a
  // response from it.
  void connectToPrimary() {
    connectingToPrimary();

    // Connect to the primary and receive a response.
    EXPECT_CALL(grpc_mux_callbacks_, onStreamEstablished());
    primary_callbacks_->onStreamEstablished();
    std::unique_ptr<ResponseType> response(std::make_unique<ResponseType>());
    response->set_version_info("456");
    Stats::TestUtil::TestStore stats;
    ControlPlaneStats cp_stats{Utility::generateControlPlaneStats(*stats.rootScope())};
    EXPECT_CALL(grpc_mux_callbacks_, onDiscoveryResponse(_, _));
    primary_callbacks_->onDiscoveryResponse(std::move(response), cp_stats);
  }

  // Get to a connecting to failover state.
  // Attempts to establish a stream to the primary source, observes 2 consecutive failures,
  // attempts to connect to the failover source, but has yet received a response from it.
  void connectingToFailover() {
    // Initial connection attempt.
    EXPECT_CALL(primary_stream_, establishNewStream());
    EXPECT_CALL(failover_stream_, establishNewStream()).Times(0);
    grpc_mux_failover_->establishNewStream();

    // First disconnect.
    EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure(false));
    primary_callbacks_->onEstablishmentFailure(false);

    // Emulate a retry that ends with a second disconnect. It should close the
    // primary stream and try to establish the failover stream.
    EXPECT_CALL(primary_stream_, closeStream());
    EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure(false));
    EXPECT_CALL(primary_stream_, establishNewStream()).Times(0);
    EXPECT_CALL(failover_stream_, establishNewStream());
    primary_callbacks_->onEstablishmentFailure(false);
  }

  // Successfully connect to the failover source.
  // Attempts to establish a stream to the primary source, observes 2 consecutive failures,
  // attempts to connect to the failover source, and receives a response from it.
  void connectToFailover() {
    connectingToFailover();

    // Connect to the primary and receive a response.
    EXPECT_CALL(grpc_mux_callbacks_, onStreamEstablished());
    failover_callbacks_->onStreamEstablished();
    std::unique_ptr<ResponseType> response(std::make_unique<ResponseType>());
    response->set_version_info("456");
    Stats::TestUtil::TestStore stats;
    ControlPlaneStats cp_stats{Utility::generateControlPlaneStats(*stats.rootScope())};
    EXPECT_CALL(grpc_mux_callbacks_, onDiscoveryResponse(_, _));
    failover_callbacks_->onDiscoveryResponse(std::move(response), cp_stats);
  }

  void invokeCloseStream() {
    // A wrapper that invokes closeStream(). It is needed because closeStream()
    // is a private method, and while this class is a friend for GrpcMuxFailover,
    // the tests cannot invoke the method directly.
    grpc_mux_failover_->closeStream();
  }

  // Override a timer to emulate its expiration without waiting for it to expire.
  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::MockTimer* timer_;
  Event::TimerCb timer_cb_;

  std::unique_ptr<MockGrpcStream<RequestType, ResponseType>> primary_stream_owner_;
  std::unique_ptr<MockGrpcStream<RequestType, ResponseType>> failover_stream_owner_;
  MockGrpcStream<RequestType, ResponseType>& primary_stream_;
  MockGrpcStream<RequestType, ResponseType>& failover_stream_;
  NiceMock<MockGrpcStreamCallbacks> grpc_mux_callbacks_;
  GrpcStreamCallbacks<ResponseType>* primary_callbacks_{nullptr};
  GrpcStreamCallbacks<ResponseType>* failover_callbacks_{nullptr};
  std::unique_ptr<GrpcMuxFailover<RequestType, ResponseType>> grpc_mux_failover_;
};

// Validates that when establishing a stream, its the stream to the primary service
// that is established, and not the failover.
TEST_F(GrpcMuxFailoverTest, EstablishPrimaryStream) {
  EXPECT_CALL(primary_stream_, establishNewStream());
  grpc_mux_failover_->establishNewStream();
}

// Validates that multiple calls to establishNewStream by default are invoked
// on the primary stream, and not the failover.
TEST_F(GrpcMuxFailoverTest, MultipleEstablishPrimaryStream) {
  EXPECT_CALL(primary_stream_, establishNewStream());
  grpc_mux_failover_->establishNewStream();

  EXPECT_CALL(primary_stream_, establishNewStream());
  grpc_mux_failover_->establishNewStream();
}

// Validates that grpcStreamAvailable forwards to the primary by default.
TEST_F(GrpcMuxFailoverTest, PrimaryStreamAvailableDefault) {
  EXPECT_CALL(primary_stream_, grpcStreamAvailable()).WillOnce(Return(false));
  EXPECT_CALL(failover_stream_, grpcStreamAvailable()).Times(0);
  EXPECT_FALSE(grpc_mux_failover_->grpcStreamAvailable());
}

// Validates that grpcStreamAvailable is invoked on the primary stream when connecting to primary.
TEST_F(GrpcMuxFailoverTest, PrimaryStreamAvailableConnectingToPrimary) {
  connectingToPrimary();
  EXPECT_CALL(primary_stream_, grpcStreamAvailable()).WillOnce(Return(true));
  EXPECT_CALL(failover_stream_, grpcStreamAvailable()).Times(0);
  EXPECT_TRUE(grpc_mux_failover_->grpcStreamAvailable());
}

// Validates that a message is sent to the primary stream by default.
TEST_F(GrpcMuxFailoverTest, SendMessagePrimaryDefault) {
  // Send a message, expect it to be sent to the primary.
  RequestType msg;
  msg.set_version_info("123");
  EXPECT_CALL(primary_stream_, sendMessage(ProtoEq(msg)));
  EXPECT_CALL(failover_stream_, sendMessage(_)).Times(0);
  grpc_mux_failover_->sendMessage(msg);
}

// Validates that updating the queue size of the primary stream by default.
TEST_F(GrpcMuxFailoverTest, MaybeUpdateQueueSizePrimaryDefault) {
  EXPECT_CALL(primary_stream_, maybeUpdateQueueSizeStat(123));
  EXPECT_CALL(failover_stream_, maybeUpdateQueueSizeStat(123)).Times(0);
  grpc_mux_failover_->maybeUpdateQueueSizeStat(123);
}

// Validates that checkRateLimitAllowsDrain is invoked on the primary stream
// by default.
TEST_F(GrpcMuxFailoverTest, CheckRateLimitPrimaryStreamDefault) {
  EXPECT_CALL(primary_stream_, checkRateLimitAllowsDrain()).WillOnce(Return(false));
  EXPECT_CALL(failover_stream_, checkRateLimitAllowsDrain()).Times(0);
  EXPECT_FALSE(grpc_mux_failover_->checkRateLimitAllowsDrain());
}

// Validate that upon failure of first connection to the primary, the second
// will still be to the primary.
TEST_F(GrpcMuxFailoverTest, AttemptPrimaryAfterPrimaryInitialFailure) {
  connectingToPrimary();

  // First disconnect.
  EXPECT_CALL(primary_stream_, closeStream()).Times(0);
  EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure(false));
  EXPECT_CALL(failover_stream_, establishNewStream()).Times(0);
  primary_callbacks_->onEstablishmentFailure(false);
}

// Validate that upon failure of the second connection to the primary, the
// failover will be attempted.
TEST_F(GrpcMuxFailoverTest, AttemptFailoverAfterPrimaryTwoFailures) {
  connectingToPrimary();

  // First disconnect.
  EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure(false));
  primary_callbacks_->onEstablishmentFailure(false);

  // Emulate a retry that ends with a second disconnect. It should close the
  // primary stream and try to establish the failover stream.
  EXPECT_CALL(primary_stream_, closeStream());
  EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure(false));
  EXPECT_CALL(primary_stream_, establishNewStream()).Times(0);
  EXPECT_CALL(failover_stream_, establishNewStream());
  primary_callbacks_->onEstablishmentFailure(false);
}

// Validate that starting from the second failure to reach the primary,
// the attempts will alternate between the failover and the primary.
TEST_F(GrpcMuxFailoverTest, AlternatingBetweenFailoverAndPrimary) {
  connectingToPrimary();

  // First disconnect.
  EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure(false));
  primary_callbacks_->onEstablishmentFailure(false);

  // Emulate a 5 times disconnects.
  for (int attempt = 0; attempt < 5; ++attempt) {
    if (attempt % 2 == 0) {
      // Emulate a primary source failure that will result in an attempt to
      // connect to the failover. It should close the primary stream, and
      // try to establish the failover stream.
      EXPECT_CALL(primary_stream_, closeStream());
      EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure(false));
      EXPECT_CALL(primary_stream_, establishNewStream()).Times(0);
      EXPECT_CALL(failover_stream_, establishNewStream());
      primary_callbacks_->onEstablishmentFailure(false);
    } else {
      // Emulate a failover source failure that will result in an attempt to
      // connect to the primary. It should close the failover stream, and
      // enable the retry timer.
      EXPECT_CALL(failover_stream_, closeStream());
      EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure(false));
      EXPECT_CALL(failover_stream_, establishNewStream()).Times(0);
      EXPECT_CALL(*timer_, enableTimer(_, _));
      failover_callbacks_->onEstablishmentFailure(false);
      // Emulate a timer tick, which should try to reconnect to the primary
      // stream.
      EXPECT_CALL(primary_stream_, establishNewStream());
      timer_cb_();
    }
  }
}

// Validate that after the primary is available (a response is received), all
// reconnect attempts will be to the primary.
TEST_F(GrpcMuxFailoverTest, PrimaryOnlyAttemptsAfterPrimaryAvailable) {
  connectToPrimary();

  // Emulate 5 disconnects, and ensure the failover connection isn't attempted.
  for (int attempt = 0; attempt < 5; ++attempt) {
    // Emulate a primary source failure that will not result in an attempt to
    // connect to the failover. It should not close the primary stream (so
    // the retry mechanism will kick in).
    EXPECT_CALL(primary_stream_, closeStream()).Times(0);
    EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure(true));
    EXPECT_CALL(failover_stream_, establishNewStream()).Times(0);
    primary_callbacks_->onEstablishmentFailure(false);
  }

  // Emulate a call to establishNewStream().
  EXPECT_CALL(primary_stream_, establishNewStream());
  EXPECT_CALL(failover_stream_, establishNewStream()).Times(0);
  grpc_mux_failover_->establishNewStream();
}

// Validate that after the failover is available (a response is received), Envoy
// will try to reconnect to the primary (and then failover), and keep
// alternating between the two.
TEST_F(GrpcMuxFailoverTest, AlternatingPrimaryAndFailoverAttemptsAfterFailoverAvailable) {
  connectToFailover();

  // Emulate a 5 times disconnects.
  for (int attempt = 0; attempt < 5; ++attempt) {
    if (attempt % 2 == 0) {
      // Emulate a failover source failure that will result in an attempt to
      // connect to the primary. It should close the failover stream, and
      // enable the retry timer.
      EXPECT_CALL(failover_stream_, closeStream());
      EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure(false));
      EXPECT_CALL(failover_stream_, establishNewStream()).Times(0);
      EXPECT_CALL(*timer_, enableTimer(_, _));
      failover_callbacks_->onEstablishmentFailure(false);
      // Emulate a timer tick, which should try to reconnect to the primary
      // stream.
      EXPECT_CALL(primary_stream_, establishNewStream());
      timer_cb_();
    } else {
      // Emulate a primary source failure that will result in an attempt to
      // connect to the failover. It should close the primary stream, and
      // try to establish the failover stream.
      EXPECT_CALL(primary_stream_, closeStream());
      // Expecting "true" to be passed as it was previously connected to the
      // failover.
      EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure(true));
      EXPECT_CALL(primary_stream_, establishNewStream()).Times(0);
      EXPECT_CALL(failover_stream_, establishNewStream());
      primary_callbacks_->onEstablishmentFailure(false);
    }
  }

  // Last attempt ended with failing to establish a failover stream,
  // emulate a successful primary stream.
  EXPECT_CALL(failover_stream_, establishNewStream()).Times(0);
  EXPECT_CALL(primary_stream_, establishNewStream());
  grpc_mux_failover_->establishNewStream();
}

// Validation that when envoy.reloadable_features.xds_failover_to_primary_enabled is disabled
// and after the failover is available (a response is received), Envoy will only
// try to reconnect to the failover.
// This test will be removed once envoy.reloadable_features.xds_failover_to_primary_enabled
// is deprecated.
TEST_F(GrpcMuxFailoverTest, StickToFailoverAfterFailoverAvailable) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.xds_failover_to_primary_enabled", "false"}});
  connectToFailover();

  // Emulate 5 disconnects, and ensure the primary reconnection isn't attempted.
  for (int attempt = 0; attempt < 5; ++attempt) {
    // Emulate a failover source failure that will not result in an attempt to
    // connect to the primary. It should not close the failover stream (so
    // the retry mechanism will kick in).
    EXPECT_CALL(failover_stream_, closeStream()).Times(0);
    EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure(true));
    EXPECT_CALL(primary_stream_, establishNewStream()).Times(0);
    failover_callbacks_->onEstablishmentFailure(true);
  }

  // Emulate a call to establishNewStream() of the failover stream.
  EXPECT_CALL(primary_stream_, establishNewStream()).Times(0);
  EXPECT_CALL(failover_stream_, establishNewStream());
  grpc_mux_failover_->establishNewStream();
}

// Validates that multiple calls to establishNewStream when connecting to the
// failover are invoked on the failover stream, and not the primary.
TEST_F(GrpcMuxFailoverTest, MultipleEstablishFailoverStream) {
  connectingToFailover();

  EXPECT_CALL(failover_stream_, establishNewStream());
  grpc_mux_failover_->establishNewStream();

  EXPECT_CALL(failover_stream_, establishNewStream());
  grpc_mux_failover_->establishNewStream();
}

// Validates that after failover attempt failure, the timer is disabled when
// an external attempt to reconnect is performed.
TEST_F(GrpcMuxFailoverTest, TimerDisabledUponExternalReconnect) {
  connectingToFailover();

  // Fail the attempt to connect to the failover.
  EXPECT_CALL(failover_stream_, closeStream());
  EXPECT_CALL(grpc_mux_callbacks_, onEstablishmentFailure(false));
  EXPECT_CALL(failover_stream_, establishNewStream()).Times(0);
  EXPECT_CALL(*timer_, enableTimer(_, _));
  failover_callbacks_->onEstablishmentFailure(false);

  // Attempt to reconnect again.
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(primary_stream_, establishNewStream());
  grpc_mux_failover_->establishNewStream();
}

// Validates that grpcStreamAvailable is invoked on the failover stream when connecting to failover.
TEST_F(GrpcMuxFailoverTest, StreamAvailableConnectingToFailover) {
  connectingToFailover();

  // Ensure that grpcStreamAvailable is invoked on the failover.
  EXPECT_CALL(primary_stream_, grpcStreamAvailable()).Times(0);
  EXPECT_CALL(failover_stream_, grpcStreamAvailable()).WillOnce(Return(true));
  EXPECT_TRUE(grpc_mux_failover_->grpcStreamAvailable());
}

// Validates that grpcStreamAvailable is invoked on the failover stream when connected to failover.
TEST_F(GrpcMuxFailoverTest, StreamAvailableConnectedToFailover) {
  connectToFailover();

  // Ensure that grpcStreamAvailable is invoked on the failover.
  EXPECT_CALL(primary_stream_, grpcStreamAvailable()).Times(0);
  EXPECT_CALL(failover_stream_, grpcStreamAvailable()).WillOnce(Return(true));
  EXPECT_TRUE(grpc_mux_failover_->grpcStreamAvailable());
}

// Validates that grpcStreamAvailable is invoked on the primary stream when connected to primary.
TEST_F(GrpcMuxFailoverTest, StreamAvailableConnectedToPrimary) {
  connectToPrimary();

  // Ensure that grpcStreamAvailable is invoked on the failover.
  EXPECT_CALL(primary_stream_, grpcStreamAvailable()).WillOnce(Return(true));
  EXPECT_CALL(failover_stream_, grpcStreamAvailable()).Times(0);
  EXPECT_TRUE(grpc_mux_failover_->grpcStreamAvailable());
}

// Validates that sendMessage is invoked on the failover stream when connecting to failover.
TEST_F(GrpcMuxFailoverTest, SendMessageConnectingToFailover) {
  connectingToFailover();

  // Ensure that grpcStreamAvailable is invoked on the failover.
  RequestType msg;
  msg.set_version_info("123");
  EXPECT_CALL(primary_stream_, sendMessage(_)).Times(0);
  EXPECT_CALL(failover_stream_, sendMessage(ProtoEq(msg)));
  grpc_mux_failover_->sendMessage(msg);
}

// Validates that sendMessage is invoked on the failover stream when connected to failover.
TEST_F(GrpcMuxFailoverTest, SendMessageConnectedToFailover) {
  connectToFailover();

  // Ensure that grpcStreamAvailable is invoked on the failover.
  RequestType msg;
  msg.set_version_info("123");
  EXPECT_CALL(primary_stream_, sendMessage(_)).Times(0);
  EXPECT_CALL(failover_stream_, sendMessage(ProtoEq(msg)));
  grpc_mux_failover_->sendMessage(msg);
}

// Validates that sendMessage is invoked on the primary stream when connected to primary.
TEST_F(GrpcMuxFailoverTest, SendMessageConnectedToPrimary) {
  connectToPrimary();

  // Ensure that grpcStreamAvailable is invoked on the failover.
  RequestType msg;
  msg.set_version_info("123");
  EXPECT_CALL(primary_stream_, sendMessage(ProtoEq(msg)));
  EXPECT_CALL(failover_stream_, sendMessage(_)).Times(0);
  grpc_mux_failover_->sendMessage(msg);
}

// Validates that updating the queue size of the failover stream is invoked when
// connecting to failover.
TEST_F(GrpcMuxFailoverTest, MaybeUpdateQueueSizeConnectingToFailover) {
  connectingToFailover();

  EXPECT_CALL(primary_stream_, maybeUpdateQueueSizeStat(_)).Times(0);
  EXPECT_CALL(failover_stream_, maybeUpdateQueueSizeStat(123));
  grpc_mux_failover_->maybeUpdateQueueSizeStat(123);
}

// Validates that updating the queue size of the failover stream is invoked when
// connected to failover.
TEST_F(GrpcMuxFailoverTest, MaybeUpdateQueueSizeConnectedToFailover) {
  connectToFailover();

  EXPECT_CALL(primary_stream_, maybeUpdateQueueSizeStat(_)).Times(0);
  EXPECT_CALL(failover_stream_, maybeUpdateQueueSizeStat(123));
  grpc_mux_failover_->maybeUpdateQueueSizeStat(123);
}

// Validates that updating the queue size of the primary stream is invoked when
// connected to primary.
TEST_F(GrpcMuxFailoverTest, MaybeUpdateQueueSizeConnectedToPrimary) {
  connectToPrimary();

  EXPECT_CALL(primary_stream_, maybeUpdateQueueSizeStat(123));
  EXPECT_CALL(failover_stream_, maybeUpdateQueueSizeStat(_)).Times(0);
  grpc_mux_failover_->maybeUpdateQueueSizeStat(123);
}

// Validates that checkRateLimitAllowsDrain is invoked on the failover stream
// when connecting to failover.
TEST_F(GrpcMuxFailoverTest, CheckRateLimitConnectingToFailover) {
  connectingToFailover();

  EXPECT_CALL(primary_stream_, checkRateLimitAllowsDrain()).Times(0);
  EXPECT_CALL(failover_stream_, checkRateLimitAllowsDrain()).WillOnce(Return(false));
  EXPECT_FALSE(grpc_mux_failover_->checkRateLimitAllowsDrain());
}

// Validates that checkRateLimitAllowsDrain is invoked on the failover stream
// when connected to failover.
TEST_F(GrpcMuxFailoverTest, CheckRateLimitConnectedToFailover) {
  connectToFailover();

  EXPECT_CALL(primary_stream_, checkRateLimitAllowsDrain()).Times(0);
  EXPECT_CALL(failover_stream_, checkRateLimitAllowsDrain()).WillOnce(Return(false));
  EXPECT_FALSE(grpc_mux_failover_->checkRateLimitAllowsDrain());
}

// Validates that checkRateLimitAllowsDrain is invoked on the primary stream
// when connected to primary.
TEST_F(GrpcMuxFailoverTest, CheckRateLimitConnectedToPrimary) {
  connectToPrimary();

  EXPECT_CALL(primary_stream_, checkRateLimitAllowsDrain()).WillOnce(Return(false));
  EXPECT_CALL(failover_stream_, checkRateLimitAllowsDrain()).Times(0);
  EXPECT_FALSE(grpc_mux_failover_->checkRateLimitAllowsDrain());
}

// Validates that onWritable callback is invoked on the failover stream
// callbacks when connecting to failover.
TEST_F(GrpcMuxFailoverTest, OnWriteableConnectingToFailoverInvoked) {
  connectingToFailover();

  EXPECT_CALL(grpc_mux_callbacks_, onWriteable());
  failover_callbacks_->onWriteable();
}

// Validates that onWritable callback is invoked on the failover stream
// callbacks when connected to failover.
TEST_F(GrpcMuxFailoverTest, OnWriteableConnectedToFailoverInvoked) {
  connectToFailover();

  EXPECT_CALL(grpc_mux_callbacks_, onWriteable());
  failover_callbacks_->onWriteable();
}

// Validates that onWritable callback is invoked on the primary stream
// callbacks when connected to primary.
TEST_F(GrpcMuxFailoverTest, OnWriteableConnectedToPrimaryInvoked) {
  connectToPrimary();

  EXPECT_CALL(grpc_mux_callbacks_, onWriteable());
  primary_callbacks_->onWriteable();
}

// Validates that when connected to primary, a subsequent call to establishNewStream
// will not try to recreate the stream.
TEST_F(GrpcMuxFailoverTest, NoRecreateStreamWhenConnectedToPrimary) {
  connectToPrimary();
  EXPECT_CALL(primary_stream_, establishNewStream()).Times(0);
  EXPECT_CALL(failover_stream_, establishNewStream()).Times(0);
  grpc_mux_failover_->establishNewStream();
}

// Validates that when connected to failover, a subsequent call to establishNewStream
// will not try to recreate the stream.
TEST_F(GrpcMuxFailoverTest, NoRecreateStreamWhenConnectedToFailover) {
  connectToFailover();
  EXPECT_CALL(primary_stream_, establishNewStream()).Times(0);
  EXPECT_CALL(failover_stream_, establishNewStream()).Times(0);
  grpc_mux_failover_->establishNewStream();
}

// Validates that closing the stream when connected to primary closes the
// primary stream.
TEST_F(GrpcMuxFailoverTest, CloseStreamWhenConnectedToPrimary) {
  connectToPrimary();
  EXPECT_CALL(primary_stream_, closeStream());
  EXPECT_CALL(failover_stream_, closeStream()).Times(0);
  invokeCloseStream();
}

// Validates that closing the stream when connected to failover closes the
// failover stream.
TEST_F(GrpcMuxFailoverTest, CloseStreamWhenConnectedToFailover) {
  connectToFailover();
  EXPECT_CALL(primary_stream_, closeStream()).Times(0);
  EXPECT_CALL(failover_stream_, closeStream());
  invokeCloseStream();
}

} // namespace Config
} // namespace Envoy
