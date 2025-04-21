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

  // Initial call should succeed
  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  // Now simulate a gRPC error
  // With failure_mode_allow=true, connection should NOT be closed
  EXPECT_CALL(connection_, close(_, _)).Times(0);

  // Trigger onGrpcError callback
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

  // Set up the client to return our mock stream
  EXPECT_CALL(*client_, start(_, _, _, _))
      .WillOnce(testing::Invoke(
          [&](ExternalProcessorCallbacks&, const Grpc::GrpcServiceConfigWithHashKey&,
              Http::AsyncClient::StreamOptions&,
              Http::StreamFilterSidestreamWatermarkCallbacks&) -> ExternalProcessorStreamPtr {
            return std::move(stream);
          }));

  // Initial call should succeed
  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  // With failure_mode_allow=false, connection should be closed on gRPC error
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::FlushWrite, "ext_proc_grpc_error"));

  // Trigger onGrpcError callback
  filter_->onGrpcError(Grpc::Status::Internal, "test error");
}

// Test normal processing flow
TEST_F(NetworkExtProcFilterTest, NormalProcessing) {
  // Set up a stream
  auto stream = std::make_unique<NiceMock<MockExternalProcessorStream>>();
  auto* stream_ptr = stream.get();

  // Expect the send method to be called when processing data
  EXPECT_CALL(*stream_ptr, send(_, false));

  // Set up the client to return our mock stream
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

  // Deliver the response
  filter_->onReceiveMessage(
      std::make_unique<envoy::service::network_ext_proc::v3::ProcessingResponse>(response));
}

} // namespace
} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
