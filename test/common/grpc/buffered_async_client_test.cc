#include <chrono>
#include <cstdint>
#include <memory>

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

constexpr uint32_t kPendingBufferSizeLimit = 1 << 16;

class BufferedAsyncClientTest : public testing::Test {
public:
  BufferedAsyncClientTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")),
        method_descriptor_(helloworld::Greeter::descriptor()->FindMethodByName("SayHello")) {
    config_.mutable_envoy_grpc()->set_cluster_name("test_cluster");

    cm_.initializeThreadLocalClusters({"test_cluster"});
    ON_CALL(cm_.thread_local_cluster_, httpAsyncClient()).WillByDefault(ReturnRef(http_client_));
  }

  void SetUp() override {
    EXPECT_CALL(http_client_, start(_, _)).WillOnce(Return(&http_stream_));
    EXPECT_CALL(http_stream_, sendHeaders(_, _));
    EXPECT_CALL(http_stream_, reset());

    raw_client_ = std::make_shared<AsyncClientImpl>(cm_, config_, dispatcher_->timeSource());
    client_ = std::make_unique<AsyncClient<helloworld::HelloRequest, helloworld::HelloReply>>(
        raw_client_);
  }

  void prepareBufferedClient(uint32_t buffer_size, std::chrono::milliseconds ttl) {
    buffered_client_ =
        std::make_unique<BufferedAsyncClient<helloworld::HelloRequest, helloworld::HelloReply>>(
            buffer_size, *method_descriptor_, callback_, *client_, *dispatcher_, ttl);
  }

  void bufferNewMessage(absl::optional<uint32_t> expected_message_id) {
    helloworld::HelloRequest request;
    request.set_name("Alice");
    EXPECT_EQ(expected_message_id, buffered_client_->bufferMessage(request));
  }

  void validateBuffer(uint32_t expected_buffered_count, uint32_t expected_pending_count) {
    const auto buffer = buffered_client_->messageBuffer();
    uint32_t buffered_count = 0;
    uint32_t pending_count = 0;

    for (const auto& message : buffer) {
      switch (message.second.first) {
      case BufferState::Buffered:
        ++buffered_count;
        break;
      case BufferState::PendingFlush:
        ++pending_count;
        break;
      default:
        break;
      }
    }

    EXPECT_EQ(buffered_count, expected_buffered_count);
    EXPECT_EQ(pending_count, expected_pending_count);
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  const Protobuf::MethodDescriptor* method_descriptor_;
  envoy::config::core::v3::GrpcService config_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Http::MockAsyncClient> http_client_;
  Http::MockAsyncClientStream http_stream_;
  std::shared_ptr<AsyncClientImpl> raw_client_;
  std::unique_ptr<AsyncClient<helloworld::HelloRequest, helloworld::HelloReply>> client_;
  NiceMock<MockAsyncStreamCallbacks<helloworld::HelloReply>> callback_;
  std::unique_ptr<BufferedAsyncClient<helloworld::HelloRequest, helloworld::HelloReply>>
      buffered_client_;
};

TEST_F(BufferedAsyncClientTest, BasicSendFlow) {
  EXPECT_CALL(http_stream_, sendData(_, _));
  EXPECT_CALL(http_stream_, isAboveWriteBufferHighWatermark()).WillRepeatedly(Return(false));

  prepareBufferedClient(kPendingBufferSizeLimit, std::chrono::milliseconds(1000));
  bufferNewMessage(0);
  validateBuffer(1, 0);

  EXPECT_EQ(1, buffered_client_->sendBufferedMessages().size());
  EXPECT_TRUE(buffered_client_->hasActiveStream());
  validateBuffer(0, 1);

  // Pending messages should not be re-sent.
  EXPECT_EQ(0, buffered_client_->sendBufferedMessages().size());
  validateBuffer(0, 1);

  // It will call onError().
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  validateBuffer(1, 0);

  // If we call onSuccess(), after onError() called,
  // onSuccess() do not affect to the buffer.
  buffered_client_->onSuccess(0);
  validateBuffer(1, 0);

  EXPECT_CALL(http_stream_, sendData(_, _)).Times(2);
  bufferNewMessage(1);
  validateBuffer(2, 0);

  const auto inflight_message_ids = buffered_client_->sendBufferedMessages();
  EXPECT_EQ(2, inflight_message_ids.size());
  validateBuffer(0, 2);

  // Clear existing messages.
  for (auto&& id : inflight_message_ids) {
    buffered_client_->onSuccess(id);
  }
  validateBuffer(0, 0);

  // It will call onError().
  // But messages have been already cleared.
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  validateBuffer(0, 0);

  // Successfully cleared pending messages.
  EXPECT_EQ(0, buffered_client_->sendBufferedMessages().size());
  validateBuffer(0, 0);
}

TEST_F(BufferedAsyncClientTest, BufferLimitExceeded) {
  EXPECT_CALL(http_stream_, isAboveWriteBufferHighWatermark()).WillOnce(Return(false));

  prepareBufferedClient(0, std::chrono::milliseconds(1000));
  bufferNewMessage(absl::nullopt);

  EXPECT_EQ(0, buffered_client_->sendBufferedMessages().size());
  EXPECT_TRUE(buffered_client_->hasActiveStream());
}

TEST_F(BufferedAsyncClientTest, BufferHighWatermarkTest) {
  EXPECT_CALL(http_stream_, isAboveWriteBufferHighWatermark()).WillOnce(Return(true));

  prepareBufferedClient(kPendingBufferSizeLimit, std::chrono::milliseconds(1000));
  bufferNewMessage(0);

  EXPECT_EQ(0, buffered_client_->sendBufferedMessages().size());
  EXPECT_TRUE(buffered_client_->hasActiveStream());
}

} // namespace
} // namespace Grpc
} // namespace Envoy
