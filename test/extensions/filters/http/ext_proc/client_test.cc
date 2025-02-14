#include "envoy/config/core/v3/grpc_service.pb.h"

#include "source/common/grpc/common.h"
#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/http/ext_proc/client_impl.h"

#include "test/mocks/grpc/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/stream_info/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;

using testing::Invoke;
using testing::Unused;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

class ExtProcStreamTest : public testing::Test, public ExternalProcessorCallbacks {
public:
  ~ExtProcStreamTest() override = default;

protected:
  void SetUp() override {
    grpc_service_.mutable_envoy_grpc()->set_cluster_name("test");
    config_with_hash_key_.setConfig(grpc_service_);

    EXPECT_CALL(client_manager_, getOrCreateRawAsyncClientWithHashKey(_, _, _))
        .WillOnce(Invoke(this, &ExtProcStreamTest::doFactory));

    client_ =
        std::make_unique<ExternalProcessorClientImpl>(client_manager_, *stats_store_.rootScope());

    watermark_callbacks_.setDecoderFilterCallbacks(&decoder_callbacks_);
    watermark_callbacks_.setEncoderFilterCallbacks(&encoder_callbacks_);
  }

  Grpc::RawAsyncClientSharedPtr doFactory(Unused, Unused, Unused) {
    auto async_client = std::make_shared<Grpc::MockAsyncClient>();
    EXPECT_CALL(*async_client,
                startRaw("envoy.service.ext_proc.v3.ExternalProcessor", "Process", _, _))
        .WillOnce(Invoke(this, &ExtProcStreamTest::doStartRaw));
    return async_client;
  }

  Grpc::RawAsyncStream* doStartRaw(Unused, Unused, Grpc::RawAsyncStreamCallbacks& callbacks,
                                   const Http::AsyncClient::StreamOptions&) {
    stream_callbacks_ = &callbacks;
    return &stream_;
  }

  // ExternalProcessorCallbacks
  void onReceiveMessage(std::unique_ptr<ProcessingResponse>&& response) override {
    last_response_ = std::move(response);
  }

  void onGrpcError(Grpc::Status::GrpcStatus status, const std::string& message) override {
    grpc_status_ = status;
    grpc_error_message_ = message;
  }

  void onGrpcClose() override { grpc_closed_ = true; }
  void logStreamInfo() override {}
  void onComplete(envoy::service::ext_proc::v3::ProcessingResponse&) override {}
  void onError() override {}

  std::unique_ptr<ProcessingResponse> last_response_;
  Grpc::Status::GrpcStatus grpc_status_ = Grpc::Status::WellKnownGrpcStatus::Ok;
  std::string grpc_error_message_;
  bool grpc_closed_ = false;

  envoy::config::core::v3::GrpcService grpc_service_;
  Grpc::GrpcServiceConfigWithHashKey config_with_hash_key_;
  ExternalProcessorClientPtr client_;
  Grpc::MockAsyncClientManager client_manager_;
  testing::NiceMock<Grpc::MockAsyncStream> stream_;
  Grpc::RawAsyncStreamCallbacks* stream_callbacks_;
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  testing::NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Http::StreamFilterSidestreamWatermarkCallbacks watermark_callbacks_;

  testing::NiceMock<Stats::MockStore> stats_store_;
};

TEST_F(ExtProcStreamTest, OpenCloseStream) {
  Http::AsyncClient::ParentContext parent_context;
  parent_context.stream_info = &stream_info_;
  auto options = Http::AsyncClient::StreamOptions().setParentContext(parent_context);
  auto stream = client_->start(*this, config_with_hash_key_, options, watermark_callbacks_);
  EXPECT_CALL(stream_, closeStream());
  EXPECT_CALL(stream_, resetStream());
  stream->close();
}

TEST_F(ExtProcStreamTest, SendToStream) {
  Http::AsyncClient::ParentContext parent_context;
  parent_context.stream_info = &stream_info_;
  auto options = Http::AsyncClient::StreamOptions().setParentContext(parent_context);
  auto stream = client_->start(*this, config_with_hash_key_, options, watermark_callbacks_);
  // Send something and ensure that we get it. Doesn't really matter what.
  EXPECT_CALL(stream_, sendMessageRaw_(_, false));
  ProcessingRequest req;
  stream->send(std::move(req), false);
  EXPECT_CALL(stream_, closeStream());
  EXPECT_CALL(stream_, resetStream());
  stream->close();
}

TEST_F(ExtProcStreamTest, SendAndClose) {
  Http::AsyncClient::ParentContext parent_context;
  parent_context.stream_info = &stream_info_;
  auto options = Http::AsyncClient::StreamOptions().setParentContext(parent_context);
  auto stream = client_->start(*this, config_with_hash_key_, options, watermark_callbacks_);
  EXPECT_CALL(stream_, sendMessageRaw_(_, true));
  ProcessingRequest req;
  stream->send(std::move(req), true);
}

TEST_F(ExtProcStreamTest, ReceiveFromStream) {
  Http::AsyncClient::ParentContext parent_context;
  parent_context.stream_info = &stream_info_;
  auto options = Http::AsyncClient::StreamOptions().setParentContext(parent_context);
  auto stream = client_->start(*this, config_with_hash_key_, options, watermark_callbacks_);
  ASSERT_NE(stream_callbacks_, nullptr);
  // Send something and ensure that we get it. Doesn't really matter what.
  ProcessingResponse resp;

  // These do nothing at the moment
  auto empty_request_headers = Http::RequestHeaderMapImpl::create();
  stream_callbacks_->onCreateInitialMetadata(*empty_request_headers);

  auto empty_response_headers = Http::ResponseHeaderMapImpl::create();
  stream_callbacks_->onReceiveInitialMetadata(std::move(empty_response_headers));

  auto response_buf = Grpc::Common::serializeMessage(resp);
  EXPECT_FALSE(last_response_);
  EXPECT_FALSE(grpc_closed_);
  EXPECT_EQ(grpc_status_, 0);
  EXPECT_EQ(grpc_error_message_, "");
  EXPECT_TRUE(stream_callbacks_->onReceiveMessageRaw(std::move(response_buf)));
  EXPECT_TRUE(last_response_);
  EXPECT_FALSE(grpc_closed_);
  EXPECT_EQ(grpc_status_, 0);
  EXPECT_EQ(grpc_error_message_, "");
  auto empty_response_trailers = Http::ResponseTrailerMapImpl::create();
  stream_callbacks_->onReceiveTrailingMetadata(std::move(empty_response_trailers));

  EXPECT_CALL(stream_, closeStream());
  EXPECT_CALL(stream_, resetStream());
  stream->close();
}

TEST_F(ExtProcStreamTest, StreamClosed) {
  Http::AsyncClient::ParentContext parent_context;
  parent_context.stream_info = &stream_info_;
  auto options = Http::AsyncClient::StreamOptions().setParentContext(parent_context);
  auto stream = client_->start(*this, config_with_hash_key_, options, watermark_callbacks_);
  ASSERT_NE(stream_callbacks_, nullptr);
  EXPECT_FALSE(last_response_);
  EXPECT_FALSE(grpc_closed_);
  EXPECT_EQ(grpc_status_, 0);
  EXPECT_EQ(grpc_error_message_, "");
  stream_callbacks_->onRemoteClose(0, "");
  EXPECT_FALSE(last_response_);
  EXPECT_TRUE(grpc_closed_);
  EXPECT_EQ(grpc_status_, 0);
  EXPECT_EQ(grpc_error_message_, "");
  stream->close();
}

TEST_F(ExtProcStreamTest, StreamError) {
  Http::AsyncClient::ParentContext parent_context;
  parent_context.stream_info = &stream_info_;
  auto options = Http::AsyncClient::StreamOptions().setParentContext(parent_context);
  auto stream = client_->start(*this, config_with_hash_key_, options, watermark_callbacks_);
  ASSERT_NE(stream_callbacks_, nullptr);
  EXPECT_FALSE(last_response_);
  EXPECT_FALSE(grpc_closed_);
  EXPECT_EQ(grpc_status_, 0);
  stream_callbacks_->onRemoteClose(123, "gRPC error message");
  EXPECT_FALSE(last_response_);
  EXPECT_FALSE(grpc_closed_);
  EXPECT_EQ(grpc_status_, 123);
  EXPECT_EQ(grpc_error_message_, "gRPC error message");
  stream->close();
}

TEST_F(ExtProcStreamTest, CancelStream) {
  Http::AsyncClient::ParentContext parent_context;
  parent_context.stream_info = &stream_info_;
  auto options = Http::AsyncClient::StreamOptions().setParentContext(parent_context);

  // Create client and verify we can call cancel() without issues
  client_->cancel();

  auto stream = client_->start(*this, config_with_hash_key_, options, watermark_callbacks_);

  // Call cancel - should be safe to call multiple times
  client_->cancel();
  client_->cancel();

  // Clean up
  EXPECT_CALL(stream_, closeStream());
  stream->close();
}

TEST_F(ExtProcStreamTest, StreamInfoAndCleanup) {
  Http::AsyncClient::ParentContext parent_context;
  parent_context.stream_info = &stream_info_;
  auto options = Http::AsyncClient::StreamOptions().setParentContext(parent_context);

  // Set up default action for streamInfo()
  ON_CALL(stream_, streamInfo()).WillByDefault(testing::ReturnRef(stream_info_));

  auto stream = client_->start(*this, config_with_hash_key_, options, watermark_callbacks_);

  // Verify we can access stream info
  EXPECT_EQ(&stream->streamInfo(), &stream_info_);
  const auto& const_stream = *stream;
  EXPECT_EQ(&const_stream.streamInfo(), &stream_info_);

  // Mock stream watermark callbacks if flow control is enabled
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.grpc_side_stream_flow_control")) {
    // Mock stream for removeWatermarkCallbacks
    EXPECT_CALL(stream_, removeWatermarkCallbacks());
  }

  // Notify filter destroy - should trigger cleanup
  stream->notifyFilterDestroy();

  EXPECT_CALL(stream_, closeStream());
  stream->close();
}

TEST_F(ExtProcStreamTest, WatermarkCallbacksCleanup) {
  Http::AsyncClient::ParentContext parent_context;
  parent_context.stream_info = &stream_info_;
  auto options = Http::AsyncClient::StreamOptions().setParentContext(parent_context);

  // Set up default action for streamInfo()
  ON_CALL(stream_, streamInfo()).WillByDefault(testing::ReturnRef(stream_info_));

  auto stream = client_->start(*this, config_with_hash_key_, options, watermark_callbacks_);

  const bool flow_control_enabled =
      Runtime::runtimeFeatureEnabled("envoy.reloadable_features.grpc_side_stream_flow_control");

  if (flow_control_enabled) {
    // Set up expectation before calling notifyFilterDestroy
    EXPECT_CALL(stream_, removeWatermarkCallbacks());
  }

  // Verify cleanup happens on filter destroy
  stream->notifyFilterDestroy();

  // Clean up
  EXPECT_CALL(stream_, closeStream());
  stream->close();
}

TEST_F(ExtProcStreamTest, NotifyFilterDestroyAfterRemoteClosed) {
  Http::AsyncClient::ParentContext parent_context;
  parent_context.stream_info = &stream_info_;
  auto options = Http::AsyncClient::StreamOptions().setParentContext(parent_context);
  ON_CALL(stream_, streamInfo()).WillByDefault(testing::ReturnRef(stream_info_));

  auto stream = client_->start(*this, config_with_hash_key_, options, watermark_callbacks_);

  // Close stream remotely first
  stream_callbacks_->onRemoteClose(0, "");

  // No watermark callback removals should happen since stream is already closed
  EXPECT_CALL(stream_, removeWatermarkCallbacks()).Times(0);

  stream->notifyFilterDestroy();
}

TEST_F(ExtProcStreamTest, ResetStreamWhenNotRemoteClosed) {
  Http::AsyncClient::ParentContext parent_context;
  parent_context.stream_info = &stream_info_;
  auto options = Http::AsyncClient::StreamOptions().setParentContext(parent_context);
  ON_CALL(stream_, streamInfo()).WillByDefault(testing::ReturnRef(stream_info_));

  auto stream = client_->start(*this, config_with_hash_key_, options, watermark_callbacks_);

  EXPECT_CALL(stream_, resetStream());
  stream->close();
}

TEST_F(ExtProcStreamTest, ResetStreamWhenAlreadyRemoteClosed) {
  Http::AsyncClient::ParentContext parent_context;
  parent_context.stream_info = &stream_info_;
  auto options = Http::AsyncClient::StreamOptions().setParentContext(parent_context);
  ON_CALL(stream_, streamInfo()).WillByDefault(testing::ReturnRef(stream_info_));

  auto stream = client_->start(*this, config_with_hash_key_, options, watermark_callbacks_);

  // Close remotely first
  stream_callbacks_->onRemoteClose(0, "");

  // resetStream() should not call stream_.resetStream() since already remotely closed
  EXPECT_CALL(stream_, resetStream()).Times(0);
  stream->close();
}

TEST_F(ExtProcStreamTest, OnReceiveMessageAfterFilterDestroy) {
  Http::AsyncClient::ParentContext parent_context;
  parent_context.stream_info = &stream_info_;
  auto options = Http::AsyncClient::StreamOptions().setParentContext(parent_context);
  ON_CALL(stream_, streamInfo()).WillByDefault(testing::ReturnRef(stream_info_));

  auto stream = client_->start(*this, config_with_hash_key_, options, watermark_callbacks_);

  // Destroy filter first
  stream->notifyFilterDestroy();

  // Send a message - should be ignored since filter is destroyed
  ProcessingResponse resp;
  auto response_buf = Grpc::Common::serializeMessage(resp);
  EXPECT_TRUE(stream_callbacks_->onReceiveMessageRaw(std::move(response_buf)));
  EXPECT_FALSE(last_response_); // Message should be ignored

  EXPECT_CALL(stream_, closeStream());
  stream->close();
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
