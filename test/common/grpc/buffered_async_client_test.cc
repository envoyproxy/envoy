#include "envoy/config/core/v3/grpc_service.pb.h"

#include "source/common/grpc/async_client_impl.h"
#include "source/common/grpc/buffered_async_client.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/proto/helloworld.pb.h"
#include "test/test_common/test_time.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Grpc {
namespace {

class BufferedAsyncClientTest : public testing::Test {
public:
  BufferedAsyncClientTest()
      : method_descriptor_(helloworld::Greeter::descriptor()->FindMethodByName("SayHello")) {
    config_.mutable_envoy_grpc()->set_cluster_name("test_cluster");

    cm_.initializeThreadLocalClusters({"test_cluster"});
    ON_CALL(cm_.thread_local_cluster_, httpAsyncClient()).WillByDefault(ReturnRef(http_client_));
  }

  const Protobuf::MethodDescriptor* method_descriptor_;
  envoy::config::core::v3::GrpcService config_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Http::MockAsyncClient> http_client_;
};

TEST_F(BufferedAsyncClientTest, BasicSendFlow) {
  Http::MockAsyncClientStream http_stream;
  EXPECT_CALL(http_client_, start(_, _)).WillOnce(Return(&http_stream));
  EXPECT_CALL(http_stream, sendHeaders(_, _));
  EXPECT_CALL(http_stream, isAboveWriteBufferHighWatermark()).WillOnce(Return(false));
  EXPECT_CALL(http_stream, sendData(_, _));
  EXPECT_CALL(http_stream, reset());

  DangerousDeprecatedTestTime test_time_;
  auto raw_client = std::make_shared<AsyncClientImpl>(cm_, config_, test_time_.timeSystem());
  AsyncClient<helloworld::HelloRequest, helloworld::HelloReply> client(raw_client);

  NiceMock<MockAsyncStreamCallbacks<helloworld::HelloReply>> callback;
  BufferedAsyncClient<helloworld::HelloRequest, helloworld::HelloReply> buffered_client(
      100000, *method_descriptor_, callback, client);

  helloworld::HelloRequest request;
  request.set_name("Alice");
  EXPECT_EQ(0, buffered_client.bufferMessage(request).value());
  const auto inflight_message_ids = buffered_client.sendBufferedMessages();
  EXPECT_TRUE(buffered_client.hasActiveStream());
  EXPECT_EQ(1, inflight_message_ids.size());

  // Pending messages should not be re-sent.
  EXPECT_CALL(http_stream, isAboveWriteBufferHighWatermark()).WillOnce(Return(false));
  const auto inflight_message_ids2 = buffered_client.sendBufferedMessages();
  EXPECT_EQ(0, inflight_message_ids2.size());

  // Re-buffer, and transport.
  buffered_client.onError(*inflight_message_ids.begin());

  EXPECT_CALL(http_stream, sendData(_, _)).Times(2);
  EXPECT_CALL(http_stream, isAboveWriteBufferHighWatermark()).WillOnce(Return(false));

  helloworld::HelloRequest request2;
  request2.set_name("Bob");
  EXPECT_EQ(1, buffered_client.bufferMessage(request2).value());
  auto ids2 = buffered_client.sendBufferedMessages();
  EXPECT_EQ(2, ids2.size());

  // Clear existing messages.
  for (auto&& id : ids2) {
    buffered_client.onSuccess(id);
  }

  // Successfully cleared pending messages.
  EXPECT_CALL(http_stream, isAboveWriteBufferHighWatermark()).WillOnce(Return(false));
  auto ids3 = buffered_client.sendBufferedMessages();
  EXPECT_EQ(0, ids3.size());
}

TEST_F(BufferedAsyncClientTest, BufferLimitExceeded) {
  Http::MockAsyncClientStream http_stream;
  EXPECT_CALL(http_client_, start(_, _)).WillOnce(Return(&http_stream));
  EXPECT_CALL(http_stream, sendHeaders(_, _));
  EXPECT_CALL(http_stream, isAboveWriteBufferHighWatermark()).WillOnce(Return(false));
  EXPECT_CALL(http_stream, reset());

  DangerousDeprecatedTestTime test_time_;
  auto raw_client = std::make_shared<AsyncClientImpl>(cm_, config_, test_time_.timeSystem());
  AsyncClient<helloworld::HelloRequest, helloworld::HelloReply> client(raw_client);

  NiceMock<MockAsyncStreamCallbacks<helloworld::HelloReply>> callback;
  BufferedAsyncClient<helloworld::HelloRequest, helloworld::HelloReply> buffered_client(
      0, *method_descriptor_, callback, client);

  helloworld::HelloRequest request;
  request.set_name("Alice");
  EXPECT_EQ(absl::nullopt, buffered_client.bufferMessage(request));

  EXPECT_EQ(0, buffered_client.sendBufferedMessages().size());
  EXPECT_TRUE(buffered_client.hasActiveStream());
}

TEST_F(BufferedAsyncClientTest, BufferHighWatermarkTest) {
  Http::MockAsyncClientStream http_stream;
  EXPECT_CALL(http_client_, start(_, _)).WillOnce(Return(&http_stream));
  EXPECT_CALL(http_stream, sendHeaders(_, _));
  EXPECT_CALL(http_stream, isAboveWriteBufferHighWatermark()).WillOnce(Return(true));
  EXPECT_CALL(http_stream, reset());

  DangerousDeprecatedTestTime test_time_;
  auto raw_client = std::make_shared<AsyncClientImpl>(cm_, config_, test_time_.timeSystem());
  AsyncClient<helloworld::HelloRequest, helloworld::HelloReply> client(raw_client);

  NiceMock<MockAsyncStreamCallbacks<helloworld::HelloReply>> callback;
  BufferedAsyncClient<helloworld::HelloRequest, helloworld::HelloReply> buffered_client(
      100000, *method_descriptor_, callback, client);

  helloworld::HelloRequest request;
  request.set_name("Alice");
  EXPECT_EQ(0, buffered_client.bufferMessage(request).value());

  EXPECT_EQ(0, buffered_client.sendBufferedMessages().size());
  EXPECT_TRUE(buffered_client.hasActiveStream());
}

} // namespace
} // namespace Grpc
} // namespace Envoy
