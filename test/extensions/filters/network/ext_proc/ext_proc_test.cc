#include "source/extensions/filters/network/ext_proc/ext_proc.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtProc {
namespace {

using testing::_;
using testing::ByMove;
using testing::NiceMock;
using testing::Return;
using testing::ReturnNull;
using testing::ReturnRef;

class MockExternalProcessorStream : public ExternalProcessorStream {
public:
  // Use NiceMock to avoid "uninteresting mock function call" warnings for methods we don't care
  // about
  MockExternalProcessorStream() {
    // Set default actions for methods that will be called but we don't necessarily want to verify
    ON_CALL(*this, send(_, _)).WillByDefault(Return());
    ON_CALL(*this, close()).WillByDefault(Return(true));
  }

  MOCK_METHOD(void, send,
              (envoy::service::network_ext_proc::v3::ProcessingRequest && request,
               bool end_stream));
  MOCK_METHOD(bool, close, ());
  MOCK_METHOD(bool, halfCloseAndDeleteOnRemoteClose, ());
  MOCK_METHOD(void, notifyFilterDestroy, ());
  MOCK_METHOD(const StreamInfo::StreamInfo&, streamInfo, (), (const));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, ());
};

class MockExternalProcessorClient : public ExternalProcessorClient {
public:
  MOCK_METHOD(ExternalProcessorStreamPtr, start,
              (ExternalProcessorCallbacks & callbacks,
               const Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
               Http::AsyncClient::StreamOptions& options,
               Http::StreamFilterSidestreamWatermarkCallbacks& watermark_callbacks));
  MOCK_METHOD(
      void, sendRequest,
      (envoy::service::network_ext_proc::v3::ProcessingRequest && request, bool end_stream,
       const uint64_t stream_id,
       CommonExtProc::RequestCallbacks<envoy::service::network_ext_proc::v3::ProcessingResponse>*
           callbacks,
       CommonExtProc::StreamBase* stream));
  MOCK_METHOD(void, cancel, ());
  MOCK_METHOD(const Envoy::StreamInfo::StreamInfo*, getStreamInfo, (), (const));
};

class NetworkExtProcFilterTest : public testing::Test {
public:
  NetworkExtProcFilterTest() {
    ON_CALL(read_callbacks_, connection()).WillByDefault(ReturnRef(connection_));
    ON_CALL(connection_, streamInfo()).WillByDefault(ReturnRef(stream_info_));

    // Set up basic config with failure_mode_allow = false
    auto filter_config = std::make_shared<Config>(createConfig(false));
    auto client = std::make_unique<NiceMock<MockExternalProcessorClient>>();
    client_ = client.get();
    filter_ = std::make_unique<NetworkExtProcFilter>(filter_config, std::move(client));
    filter_->initializeReadFilterCallbacks(read_callbacks_);
    filter_->initializeWriteFilterCallbacks(write_callbacks_);
  }

  // Create a config with specified failure_mode_allow setting
  envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor
  createConfig(bool failure_mode_allow) {
    envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor config;
    config.set_failure_mode_allow(failure_mode_allow);
    config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("ext_proc_server");
    return config;
  }

  // Set up a new filter with the specified failure_mode_allow setting
  void recreateFilterWithConfig(bool failure_mode_allow) {
    auto filter_config = std::make_shared<Config>(createConfig(failure_mode_allow));
    auto client = std::make_unique<NiceMock<MockExternalProcessorClient>>();
    client_ = client.get();
    filter_ = std::make_unique<NetworkExtProcFilter>(filter_config, std::move(client));
    filter_->initializeReadFilterCallbacks(read_callbacks_);
    filter_->initializeWriteFilterCallbacks(write_callbacks_);
  }

protected:
  NiceMock<Network::MockReadFilterCallbacks> read_callbacks_;
  NiceMock<Network::MockWriteFilterCallbacks> write_callbacks_;
  NiceMock<Network::MockConnection> connection_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  NiceMock<MockExternalProcessorClient>* client_;
  std::unique_ptr<NetworkExtProcFilter> filter_;
};

// Test receiving a message when processing is already complete
TEST_F(NetworkExtProcFilterTest, ReceiveMessageAfterProcessingComplete) {
  // First, mark processing as complete
  filter_->onGrpcError(Grpc::Status::Internal, "test error");

  // Create a message to send - the filter should ignore it
  auto response = std::make_unique<envoy::service::network_ext_proc::v3::ProcessingResponse>();
  auto* read_data = response->mutable_read_data();
  read_data->set_data("data");
  read_data->set_end_of_stream(false);

  // We expect the filter to ignore this message since processing is complete
  EXPECT_CALL(read_callbacks_, injectReadDataToFilterChain(_, _)).Times(0);

  filter_->onReceiveMessage(std::move(response));
}

// Test receiving a message with no data (neither read_data nor write_data)
TEST_F(NetworkExtProcFilterTest, ReceiveEmptyMessage) {
  auto stream = std::make_unique<NiceMock<MockExternalProcessorStream>>();
  auto* stream_ptr = stream.get();

  EXPECT_CALL(*stream_ptr, send(_, false));
  EXPECT_CALL(*client_, start(_, _, _, _))
      .WillOnce([&](ExternalProcessorCallbacks&, const Grpc::GrpcServiceConfigWithHashKey&,
                    Http::AsyncClient::StreamOptions&,
                    Http::StreamFilterSidestreamWatermarkCallbacks&) -> ExternalProcessorStreamPtr {
        return std::move(stream);
      });

  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  // Create a message with neither read_data nor write_data
  auto response = std::make_unique<envoy::service::network_ext_proc::v3::ProcessingResponse>();

  // Ensure no data is injected into either filter chain
  EXPECT_CALL(read_callbacks_, injectReadDataToFilterChain(_, _)).Times(0);
  EXPECT_CALL(write_callbacks_, injectWriteDataToFilterChain(_, _)).Times(0);

  filter_->onReceiveMessage(std::move(response));
}

// Test openStream method when processing is already complete
TEST_F(NetworkExtProcFilterTest, OpenStreamAfterProcessingComplete) {
  // First, mark processing as complete
  filter_->onGrpcError(Grpc::Status::Internal, "test error");

  // Should not attempt to create a new stream
  EXPECT_CALL(*client_, start(_, _, _, _)).Times(0);

  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));
}

// Test the onLogStreamInfo method
TEST_F(NetworkExtProcFilterTest, LogStreamInfo) {
  // Simply call the method to ensure coverage
  filter_->logStreamInfo();
}

// Test the onComplete method
TEST_F(NetworkExtProcFilterTest, OnComplete) {
  // Simply call the method to ensure coverage
  envoy::service::network_ext_proc::v3::ProcessingResponse response;
  filter_->onComplete(response);
}

// Test the onError method
TEST_F(NetworkExtProcFilterTest, OnError) {
  // Simply call the method to ensure coverage
  filter_->onError();
}

// Test failure mode allow behavior when stream creation fails
TEST_F(NetworkExtProcFilterTest, StreamCreationFailureWithFailureModeAllow) {
  // Recreate filter with failure_mode_allow = true
  recreateFilterWithConfig(true);

  // When client->start is called, it returns nullptr to simulate stream creation failure
  EXPECT_CALL(*client_, start(_, _, _, _)).WillOnce(ReturnNull());

  // With failure_mode_allow=true, filter should continue processing
  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  // Buffer should be untouched since we're continuing
  EXPECT_EQ(data.length(), 4);
}

// Test failure mode disallow behavior when stream creation fails
TEST_F(NetworkExtProcFilterTest, StreamCreationFailureWithFailureModeDisallow) {
  // With failure_mode_allow=false (default in setup)

  // Expect connection to be closed when stream creation fails
  EXPECT_CALL(*client_, start(_, _, _, _)).WillOnce(ReturnNull());
  EXPECT_CALL(connection_,
              close(Network::ConnectionCloseType::FlushWrite, "ext_proc_stream_error"));

  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));
}

// Test gRPC error handling with failure mode allow
TEST_F(NetworkExtProcFilterTest, GrpcErrorWithFailureModeAllow) {
  // Recreate filter with failure_mode_allow = true
  recreateFilterWithConfig(true);

  // Create a mock stream and set expectations
  auto stream = std::make_unique<NiceMock<MockExternalProcessorStream>>();
  auto* stream_ptr = stream.get();

  // Expect the send method to be called when processing data
  EXPECT_CALL(*stream_ptr, send(_, false));

  // Expect close to be called during cleanup after error
  EXPECT_CALL(*stream_ptr, close()).WillOnce(Return(true));

  // Set up the client to return our mock stream
  EXPECT_CALL(*client_, start(_, _, _, _))
      .WillOnce(testing::Invoke(
          [&](ExternalProcessorCallbacks&, const Grpc::GrpcServiceConfigWithHashKey&,
              Http::AsyncClient::StreamOptions&,
              Http::StreamFilterSidestreamWatermarkCallbacks&) -> ExternalProcessorStreamPtr {
            return std::move(stream);
          }));

  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  // Now simulate a gRPC error
  // With failure_mode_allow=true, connection should NOT be closed
  EXPECT_CALL(connection_, close(_, _)).Times(0);

  filter_->onGrpcError(Grpc::Status::Internal, "test error");

  // Next data should pass through without issues
  Buffer::OwnedImpl more_data("more");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(more_data, false));
}

// Test gRPC error handling with failure mode disallow
TEST_F(NetworkExtProcFilterTest, GrpcErrorWithFailureModeDisallow) {
  // With failure_mode_allow=false (default in setup)
  // Create a mock stream and set expectations
  auto stream = std::make_unique<NiceMock<MockExternalProcessorStream>>();
  auto* stream_ptr = stream.get();

  // Expect the send method to be called when processing data
  EXPECT_CALL(*stream_ptr, send(_, false));

  // Expect close to be called during cleanup after error
  EXPECT_CALL(*stream_ptr, close()).WillOnce(Return(true));

  EXPECT_CALL(*client_, start(_, _, _, _))
      .WillOnce(testing::Invoke(
          [&](ExternalProcessorCallbacks&, const Grpc::GrpcServiceConfigWithHashKey&,
              Http::AsyncClient::StreamOptions&,
              Http::StreamFilterSidestreamWatermarkCallbacks&) -> ExternalProcessorStreamPtr {
            return std::move(stream);
          }));

  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  // With failure_mode_allow=false, connection should be closed on gRPC error
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::FlushWrite, "ext_proc_grpc_error"));

  // Trigger onGrpcError callback
  filter_->onGrpcError(Grpc::Status::Internal, "test error");
}

// Test normal processing flow
TEST_F(NetworkExtProcFilterTest, NormalProcessing) {
  auto stream = std::make_unique<NiceMock<MockExternalProcessorStream>>();
  auto* stream_ptr = stream.get();

  // Expect the send method to be called when processing data
  EXPECT_CALL(*stream_ptr, send(_, false));

  EXPECT_CALL(*client_, start(_, _, _, _))
      .WillOnce(testing::Invoke(
          [&](ExternalProcessorCallbacks&, const Grpc::GrpcServiceConfigWithHashKey&,
              Http::AsyncClient::StreamOptions&,
              Http::StreamFilterSidestreamWatermarkCallbacks&) -> ExternalProcessorStreamPtr {
            return std::move(stream);
          }));

  // Initial call should stop iteration until we get a response
  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  // Simulate response from external processor
  envoy::service::network_ext_proc::v3::ProcessingResponse response;
  auto* read_data = response.mutable_read_data();
  read_data->set_data("modified");
  read_data->set_end_of_stream(false);

  // Expect data to be injected to the filter chain
  EXPECT_CALL(read_callbacks_, injectReadDataToFilterChain(_, false));

  filter_->onReceiveMessage(
      std::make_unique<envoy::service::network_ext_proc::v3::ProcessingResponse>(response));
}

// Test onGrpcClose handling
TEST_F(NetworkExtProcFilterTest, GrpcCloseHandling) {
  auto stream = std::make_unique<NiceMock<MockExternalProcessorStream>>();
  auto* stream_ptr = stream.get();

  EXPECT_CALL(*stream_ptr, send(_, false));
  EXPECT_CALL(*stream_ptr, close()).WillOnce(Return(true));

  EXPECT_CALL(*client_, start(_, _, _, _))
      .WillOnce(testing::Invoke(
          [&](ExternalProcessorCallbacks&, const Grpc::GrpcServiceConfigWithHashKey&,
              Http::AsyncClient::StreamOptions&,
              Http::StreamFilterSidestreamWatermarkCallbacks&) -> ExternalProcessorStreamPtr {
            return std::move(stream);
          }));

  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  // Trigger onGrpcClose and verify behavior
  filter_->onGrpcClose();

  // Subsequent data should pass through directly
  Buffer::OwnedImpl more_data("more");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(more_data, false));
}

// Test edge case with null stream in sendRequest
TEST_F(NetworkExtProcFilterTest, SendRequestWithNullStream) {
  // Set filter's stream to nullptr
  auto filter_config = std::make_shared<Config>(createConfig(false));
  auto client = std::make_unique<NiceMock<MockExternalProcessorClient>>();
  client_ = client.get();
  filter_ = std::make_unique<NetworkExtProcFilter>(filter_config, std::move(client));
  filter_->initializeReadFilterCallbacks(read_callbacks_);
  filter_->initializeWriteFilterCallbacks(write_callbacks_);

  Buffer::OwnedImpl data("test");
  EXPECT_CALL(*client_, start(_, _, _, _)).WillOnce(ReturnNull());
  EXPECT_CALL(connection_, close(_, _)).WillOnce(Return());

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));
  testing::Mock::VerifyAndClearExpectations(&connection_);
}

// Test onWrite error path
TEST_F(NetworkExtProcFilterTest, OnWriteErrorPath) {
  // Recreate filter with failure_mode_allow = true to test different error path
  recreateFilterWithConfig(true);

  // Expect client->start to return nullptr to trigger error condition
  EXPECT_CALL(*client_, start(_, _, _, _)).WillOnce(ReturnNull());

  // With failure_mode_allow=true, should continue
  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onWrite(data, false));
}

// Test updateCloseCallbackStatus edge cases
TEST_F(NetworkExtProcFilterTest, UpdateCloseCallbackStatusEdgeCases) {
  // Test multiple enable/disable for read callbacks
  Buffer::OwnedImpl data("test");

  auto stream = std::make_unique<NiceMock<MockExternalProcessorStream>>();
  auto* stream_ptr = stream.get();
  EXPECT_CALL(*stream_ptr, send(_, false));

  EXPECT_CALL(*client_, start(_, _, _, _))
      .WillOnce(testing::Invoke(
          [&](ExternalProcessorCallbacks&, const Grpc::GrpcServiceConfigWithHashKey&,
              Http::AsyncClient::StreamOptions&,
              Http::StreamFilterSidestreamWatermarkCallbacks&) -> ExternalProcessorStreamPtr {
            return std::move(stream);
          }));

  EXPECT_CALL(read_callbacks_, disableClose(true));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  // Send multiple responses to trigger disable/enable cycles
  // This will test the counter logic in updateCloseCallbackStatus
  envoy::service::network_ext_proc::v3::ProcessingResponse response;
  auto* read_data = response.mutable_read_data();
  read_data->set_data("modified");
  read_data->set_end_of_stream(false);

  EXPECT_CALL(read_callbacks_, injectReadDataToFilterChain(_, false));
  EXPECT_CALL(read_callbacks_, disableClose(false));

  filter_->onReceiveMessage(
      std::make_unique<envoy::service::network_ext_proc::v3::ProcessingResponse>(response));

  // Test write callbacks with multiple enable/disable
  EXPECT_CALL(*stream_ptr, send(_, false));
  EXPECT_CALL(write_callbacks_, disableClose(true));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onWrite(data, false));

  response = envoy::service::network_ext_proc::v3::ProcessingResponse();
  auto* write_data = response.mutable_write_data();
  write_data->set_data("modified_write");
  write_data->set_end_of_stream(false);

  EXPECT_CALL(write_callbacks_, injectWriteDataToFilterChain(_, false));
  EXPECT_CALL(write_callbacks_, disableClose(false));

  filter_->onReceiveMessage(
      std::make_unique<envoy::service::network_ext_proc::v3::ProcessingResponse>(response));
}

// Test processing mode configurations
TEST_F(NetworkExtProcFilterTest, ProcessingModeConfigurations) {
  // Test with SKIP for read processing
  auto config = createConfig(false);
  config.mutable_processing_mode()->set_process_read(
      envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::SKIP);

  auto filter_config = std::make_shared<Config>(config);
  auto client = std::make_unique<NiceMock<MockExternalProcessorClient>>();
  client_ = client.get();
  filter_ = std::make_unique<NetworkExtProcFilter>(filter_config, std::move(client));
  filter_->initializeReadFilterCallbacks(read_callbacks_);
  filter_->initializeWriteFilterCallbacks(write_callbacks_);

  // With process_read set to SKIP, data should pass through directly
  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  // Testing SKIP for write processing
  config = createConfig(false);
  config.mutable_processing_mode()->set_process_write(
      envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::SKIP);

  filter_config = std::make_shared<Config>(config);
  client = std::make_unique<NiceMock<MockExternalProcessorClient>>();
  client_ = client.get();
  filter_ = std::make_unique<NetworkExtProcFilter>(filter_config, std::move(client));
  filter_->initializeReadFilterCallbacks(read_callbacks_);
  filter_->initializeWriteFilterCallbacks(write_callbacks_);

  // With process_write set to SKIP, data should pass through directly
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onWrite(data, false));
}

} // namespace
} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
