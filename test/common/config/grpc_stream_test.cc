#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/config/grpc_stream.h"
#include "source/common/protobuf/protobuf.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/common.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Config {
namespace {

class GrpcStreamTest : public testing::Test {
protected:
  GrpcStreamTest()
      : async_client_owner_(std::make_unique<Grpc::MockAsyncClient>()),
        async_client_(async_client_owner_.get()),
        grpc_stream_(&callbacks_, std::move(async_client_owner_),
                     *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
                         "envoy.service.endpoint.v3.EndpointDiscoveryService.StreamEndpoints"),
                     random_, dispatcher_, stats_, rate_limit_settings_) {}

  NiceMock<Event::MockDispatcher> dispatcher_;
  Grpc::MockAsyncStream async_stream_;
  Stats::TestUtil::TestStore stats_;
  NiceMock<Random::MockRandomGenerator> random_;
  Envoy::Config::RateLimitSettings rate_limit_settings_;
  NiceMock<MockGrpcStreamCallbacks> callbacks_;
  std::unique_ptr<Grpc::MockAsyncClient> async_client_owner_;
  Grpc::MockAsyncClient* async_client_;
  Event::SimulatedTimeSystem time_system_;

  GrpcStream<envoy::service::discovery::v3::DiscoveryRequest,
             envoy::service::discovery::v3::DiscoveryResponse>
      grpc_stream_;
};

// Tests that establishNewStream() establishes it, a second call does nothing, and a third call
// after the stream was disconnected re-establishes it.
TEST_F(GrpcStreamTest, EstablishStream) {
  EXPECT_FALSE(grpc_stream_.grpcStreamAvailable());
  // Successful establishment
  {
    EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
    EXPECT_CALL(callbacks_, onStreamEstablished());
    grpc_stream_.establishNewStream();
    EXPECT_TRUE(grpc_stream_.grpcStreamAvailable());
  }
  // Idempotent
  {
    EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).Times(0);
    EXPECT_CALL(callbacks_, onStreamEstablished()).Times(0);
    grpc_stream_.establishNewStream();
    EXPECT_TRUE(grpc_stream_.grpcStreamAvailable());
  }
  grpc_stream_.onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Ok, "");
  EXPECT_FALSE(grpc_stream_.grpcStreamAvailable());
  // Successful re-establishment
  {
    EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
    EXPECT_CALL(callbacks_, onStreamEstablished());
    grpc_stream_.establishNewStream();
    EXPECT_TRUE(grpc_stream_.grpcStreamAvailable());
  }
}

// Tests reducing log level depending on remote close status.
TEST_F(GrpcStreamTest, LogClose) {
  EXPECT_CALL(*async_client_, destination()).WillRepeatedly(Return("test_destination"));

  // Failures with statuses that do not need special handling. They are always logged in the same
  // way and so never saved.
  {
    EXPECT_FALSE(grpc_stream_.getCloseStatus().has_value());

    // Benign status: debug.
    EXPECT_CALL(callbacks_, onEstablishmentFailure());
    EXPECT_LOG_CONTAINS("debug", "gRPC config stream to test_destination closed", {
      grpc_stream_.onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Ok, "Ok");
    });
    EXPECT_FALSE(grpc_stream_.getCloseStatus().has_value());

    // Non-retriable failure: warn.
    EXPECT_CALL(callbacks_, onEstablishmentFailure());
    EXPECT_LOG_CONTAINS("warn", "gRPC config stream to test_destination closed", {
      grpc_stream_.onRemoteClose(Grpc::Status::WellKnownGrpcStatus::NotFound, "Not Found");
    });
    EXPECT_FALSE(grpc_stream_.getCloseStatus().has_value());
  }
  // Repeated failures that warn after enough time.
  {
    // Retriable failure: debug.
    EXPECT_CALL(callbacks_, onEstablishmentFailure());
    EXPECT_LOG_CONTAINS("debug", "gRPC config stream to test_destination closed", {
      grpc_stream_.onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Unavailable, "Unavailable");
    });
    EXPECT_EQ(grpc_stream_.getCloseStatus().value(),
              Grpc::Status::WellKnownGrpcStatus::Unavailable);

    // Different retriable failure: warn.
    time_system_.advanceTimeWait(std::chrono::seconds(1));
    EXPECT_CALL(callbacks_, onEstablishmentFailure());
    EXPECT_LOG_CONTAINS("warn",
                        "stream to test_destination closed: 4, Deadline Exceeded (previously 14, "
                        "Unavailable since 1s ago)",
                        {
                          grpc_stream_.onRemoteClose(
                              Grpc::Status::WellKnownGrpcStatus::DeadlineExceeded,
                              "Deadline Exceeded");
                        });
    EXPECT_EQ(grpc_stream_.getCloseStatus().value(),
              Grpc::Status::WellKnownGrpcStatus::DeadlineExceeded);

    // Same retriable failure after a short amount of time: debug.
    time_system_.advanceTimeWait(std::chrono::seconds(1));
    EXPECT_CALL(callbacks_, onEstablishmentFailure());
    EXPECT_LOG_CONTAINS("debug", "gRPC config stream to test_destination closed", {
      grpc_stream_.onRemoteClose(Grpc::Status::WellKnownGrpcStatus::DeadlineExceeded,
                                 "Deadline Exceeded");
    });
    EXPECT_EQ(grpc_stream_.getCloseStatus().value(),
              Grpc::Status::WellKnownGrpcStatus::DeadlineExceeded);

    // Same retriable failure after a long time: warn.
    time_system_.advanceTimeWait(std::chrono::seconds(100));
    EXPECT_CALL(callbacks_, onEstablishmentFailure());
    EXPECT_LOG_CONTAINS(
        "warn",
        "gRPC config stream to test_destination closed since 101s ago: 4, Deadline Exceeded", {
          grpc_stream_.onRemoteClose(Grpc::Status::WellKnownGrpcStatus::DeadlineExceeded,
                                     "Deadline Exceeded");
        });
    EXPECT_EQ(grpc_stream_.getCloseStatus().value(),
              Grpc::Status::WellKnownGrpcStatus::DeadlineExceeded);

    // Warn again, using the newest message.
    time_system_.advanceTimeWait(std::chrono::seconds(1));
    EXPECT_CALL(callbacks_, onEstablishmentFailure());
    EXPECT_LOG_CONTAINS(
        "warn", "gRPC config stream to test_destination closed since 102s ago: 4, new message", {
          grpc_stream_.onRemoteClose(Grpc::Status::WellKnownGrpcStatus::DeadlineExceeded,
                                     "new message");
        });
    EXPECT_EQ(grpc_stream_.getCloseStatus().value(),
              Grpc::Status::WellKnownGrpcStatus::DeadlineExceeded);

    // Different retriable failure, using the most recent error message from the previous one.
    time_system_.advanceTimeWait(std::chrono::seconds(1));
    EXPECT_CALL(callbacks_, onEstablishmentFailure());
    EXPECT_LOG_CONTAINS("warn",
                        "gRPC config stream to test_destination closed: 14, Unavailable "
                        "(previously 4, new message since 103s ago)",
                        {
                          grpc_stream_.onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Unavailable,
                                                     "Unavailable");
                        });
    EXPECT_EQ(grpc_stream_.getCloseStatus().value(),
              Grpc::Status::WellKnownGrpcStatus::Unavailable);
  }

  // Successfully receiving a message clears close status.
  {
    EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
    EXPECT_CALL(callbacks_, onStreamEstablished());
    grpc_stream_.establishNewStream();
    EXPECT_TRUE(grpc_stream_.grpcStreamAvailable());
    // Status isn't cleared yet.
    EXPECT_EQ(grpc_stream_.getCloseStatus().value(),
              Grpc::Status::WellKnownGrpcStatus::Unavailable);

    auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
    grpc_stream_.onReceiveMessage(std::move(response));
    EXPECT_FALSE(grpc_stream_.getCloseStatus().has_value());
  }
}

// A failure in the underlying gRPC machinery should result in grpcStreamAvailable() false. Calling
// sendMessage would segfault.
TEST_F(GrpcStreamTest, FailToEstablishNewStream) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(nullptr));
  EXPECT_CALL(callbacks_, onEstablishmentFailure());
  grpc_stream_.establishNewStream();
  EXPECT_FALSE(grpc_stream_.grpcStreamAvailable());
}

// Checks that sendMessage correctly passes a DiscoveryRequest down to the underlying gRPC
// machinery.
TEST_F(GrpcStreamTest, SendMessage) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  grpc_stream_.establishNewStream();
  envoy::service::discovery::v3::DiscoveryRequest request;
  request.set_response_nonce("grpc_stream_test_noncense");
  EXPECT_CALL(async_stream_, sendMessageRaw_(Grpc::ProtoBufferEq(request), false));
  grpc_stream_.sendMessage(request);
}

// Tests that, upon a call of the GrpcStream::onReceiveMessage() callback, which is called by the
// underlying gRPC machinery, the received proto will make it up to the GrpcStreamCallbacks that the
// GrpcStream was given.
TEST_F(GrpcStreamTest, ReceiveMessage) {
  envoy::service::discovery::v3::DiscoveryResponse response_copy;
  response_copy.set_type_url("faketypeURL");
  auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>(response_copy);
  envoy::service::discovery::v3::DiscoveryResponse received_message;
  EXPECT_CALL(callbacks_, onDiscoveryResponse(_, _))
      .WillOnce([&received_message](
                    std::unique_ptr<envoy::service::discovery::v3::DiscoveryResponse>&& message,
                    ControlPlaneStats&) { received_message = *message; });
  grpc_stream_.onReceiveMessage(std::move(response));
  EXPECT_TRUE(TestUtility::protoEqual(response_copy, received_message));
}

// If the value has only ever been 0, the stat should remain unused, including after an attempt to
// write a 0 to it.
TEST_F(GrpcStreamTest, QueueSizeStat) {
  grpc_stream_.maybeUpdateQueueSizeStat(0);
  Stats::Gauge& pending_requests =
      stats_.gauge("control_plane.pending_requests", Stats::Gauge::ImportMode::Accumulate);
  EXPECT_FALSE(pending_requests.used());
  grpc_stream_.maybeUpdateQueueSizeStat(123);
  EXPECT_EQ(123, pending_requests.value());
  grpc_stream_.maybeUpdateQueueSizeStat(0);
  EXPECT_EQ(0, pending_requests.value());
}

// Just to add coverage to the no-op implementations of these callbacks (without exposing us to
// crashes from a badly behaved peer like PANIC("not implemented") would).
TEST_F(GrpcStreamTest, HeaderTrailerJustForCodeCoverage) {
  Http::ResponseHeaderMapPtr response_headers{new Http::TestResponseHeaderMapImpl{}};
  grpc_stream_.onReceiveInitialMetadata(std::move(response_headers));
  Http::TestRequestHeaderMapImpl request_headers;
  grpc_stream_.onCreateInitialMetadata(request_headers);
  Http::ResponseTrailerMapPtr trailers{new Http::TestResponseTrailerMapImpl{}};
  grpc_stream_.onReceiveTrailingMetadata(std::move(trailers));
}

} // namespace
} // namespace Config
} // namespace Envoy
