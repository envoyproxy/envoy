#include "envoy/api/v2/discovery.pb.h"

#include "common/config/grpc_stream.h"
#include "common/protobuf/protobuf.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/upstream/mocks.h"
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
        grpc_stream_(&xds_grpc_context_, std::move(async_client_owner_),
                     *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
                         "envoy.api.v2.EndpointDiscoveryService.StreamEndpoints"),
                     random_, dispatcher_, stats_, rate_limit_settings_,
                     [this](std::unique_ptr<envoy::api::v2::DiscoveryResponse>&& message) {
                       received_message_ = *message;
                     }) {}

  //  std::string version_;
  //  const Protobuf::MethodDescriptor* method_descriptor_;
  //  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  //  Event::MockTimer* timer_;
  //  Event::TimerCb timer_cb_;
  //  envoy::api::v2::core::Node node_;
  //  NiceMock<Config::MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment>> callbacks_;
  Grpc::MockAsyncStream async_stream_;
  //  std::string last_response_nonce_;
  //  std::vector<std::string> last_cluster_names_;
  //  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  //  Event::MockTimer* init_timeout_timer_;
  Stats::IsolatedStoreImpl stats_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Envoy::Config::RateLimitSettings rate_limit_settings_;
  NiceMock<MockXdsGrpcContext> xds_grpc_context_;
  std::unique_ptr<Grpc::MockAsyncClient> async_client_owner_;
  Grpc::MockAsyncClient* async_client_;
  envoy::api::v2::DiscoveryResponse received_message_;

  GrpcStream<envoy::api::v2::DiscoveryRequest, envoy::api::v2::DiscoveryResponse> grpc_stream_;
};

// Tests that establishNewStream() establishes it, a second call does nothing, and a third call
// after the stream was disconnected re-establishes it.
TEST_F(GrpcStreamTest, EstablishNewStream) {
  EXPECT_FALSE(grpc_stream_.grpcStreamAvailable());
  // Successful establishment
  {
    EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
    EXPECT_CALL(xds_grpc_context_, handleStreamEstablished());
    grpc_stream_.establishNewStream();
    EXPECT_TRUE(grpc_stream_.grpcStreamAvailable());
  }
  // Idempotency: do nothing (other than logging a warning) if already connected
  {
    EXPECT_CALL(*async_client_, start(_, _)).Times(0);
    EXPECT_CALL(xds_grpc_context_, handleStreamEstablished()).Times(0);
    grpc_stream_.establishNewStream();
    EXPECT_TRUE(grpc_stream_.grpcStreamAvailable());
  }
  grpc_stream_.onRemoteClose(Grpc::Status::GrpcStatus::Ok, "");
  EXPECT_FALSE(grpc_stream_.grpcStreamAvailable());
  // Successful re-establishment
  {
    EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
    EXPECT_CALL(xds_grpc_context_, handleStreamEstablished());
    grpc_stream_.establishNewStream();
    EXPECT_TRUE(grpc_stream_.grpcStreamAvailable());
  }
}

TEST_F(GrpcStreamTest, FailToEstablishNewStream) {
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(nullptr));
  EXPECT_CALL(xds_grpc_context_, handleEstablishmentFailure());
  grpc_stream_.establishNewStream();
  EXPECT_FALSE(grpc_stream_.grpcStreamAvailable());
}

TEST_F(GrpcStreamTest, SendMessage) {
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  grpc_stream_.establishNewStream();
  envoy::api::v2::DiscoveryRequest request;
  request.set_response_nonce("grpc_stream_test_noncense");
  EXPECT_CALL(async_stream_, sendMessage(ProtoEq(request), false));
  grpc_stream_.sendMessage(request);
}

TEST_F(GrpcStreamTest, ReceiveMessage) {
  envoy::api::v2::DiscoveryResponse response_copy;
  response_copy.set_type_url("faketypeURL");
  auto response = std::make_unique<envoy::api::v2::DiscoveryResponse>(response_copy);
  grpc_stream_.onReceiveMessage(std::move(response));
  EXPECT_TRUE(TestUtility::protoEqual(response_copy, received_message_));
}

TEST_F(GrpcStreamTest, QueueSizeStat) {
  grpc_stream_.maybeUpdateQueueSizeStat(0);
  EXPECT_FALSE(stats_.gauge("control_plane.pending_requests").used());
  grpc_stream_.maybeUpdateQueueSizeStat(123);
  EXPECT_EQ(123, stats_.gauge("control_plane.pending_requests").value());
  grpc_stream_.maybeUpdateQueueSizeStat(0);
  EXPECT_EQ(0, stats_.gauge("control_plane.pending_requests").value());
}

// Just to add coverage to the no-op implementations of these callbacks (without exposing us to
// crashes from a badly behaved peer like NOT_IMPLEMENTED_GCOVR_EXCL_LINE would).
TEST_F(GrpcStreamTest, HeaderTrailerJustForCodeCoverage) {
  Http::HeaderMapPtr response_headers{new Http::TestHeaderMapImpl{}};
  grpc_stream_.onReceiveInitialMetadata(std::move(response_headers));
  Http::TestHeaderMapImpl request_headers;
  grpc_stream_.onCreateInitialMetadata(request_headers);
  Http::HeaderMapPtr trailers{new Http::TestHeaderMapImpl{}};
  grpc_stream_.onReceiveTrailingMetadata(std::move(trailers));
}

} // namespace
} // namespace Config
} // namespace Envoy
