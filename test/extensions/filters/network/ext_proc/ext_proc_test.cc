#include "source/extensions/filters/network/ext_proc/ext_proc.h"

#include "test/mocks/event/mocks.h"
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
    auto filter_config = std::make_shared<Config>(createConfig(false), scope_);
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
    config.set_stat_prefix("test_ext_proc");
    config.set_failure_mode_allow(failure_mode_allow);
    config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("ext_proc_server");
    return config;
  }

  // Set up a new filter with the specified failure_mode_allow setting
  void recreateFilterWithConfig(bool failure_mode_allow) {
    auto filter_config = std::make_shared<Config>(createConfig(failure_mode_allow), scope_);
    auto client = std::make_unique<NiceMock<MockExternalProcessorClient>>();
    client_ = client.get();
    filter_ = std::make_unique<NetworkExtProcFilter>(filter_config, std::move(client));
    filter_->initializeReadFilterCallbacks(read_callbacks_);
    filter_->initializeWriteFilterCallbacks(write_callbacks_);
  }

  // Create a config with metadata options
  envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor
  createConfigWithMetadataOptions(const std::vector<std::string>& untyped_namespaces,
                                  const std::vector<std::string>& typed_namespaces) {
    envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor config;
    config.set_failure_mode_allow(false);
    config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("ext_proc_server");

    auto* metadata_options = config.mutable_metadata_options();
    auto* forwarding_namespaces = metadata_options->mutable_forwarding_namespaces();

    for (const auto& ns : untyped_namespaces) {
      forwarding_namespaces->add_untyped(ns);
    }

    for (const auto& ns : typed_namespaces) {
      forwarding_namespaces->add_typed(ns);
    }

    return config;
  }

  // Set up a new filter with metadata options
  void recreateFilterWithMetadataOptions(const std::vector<std::string>& untyped_namespaces,
                                         const std::vector<std::string>& typed_namespaces) {
    auto filter_config = std::make_shared<Config>(
        createConfigWithMetadataOptions(untyped_namespaces, typed_namespaces), scope_);
    auto client = std::make_unique<NiceMock<MockExternalProcessorClient>>();
    client_ = client.get();
    filter_ = std::make_unique<NetworkExtProcFilter>(filter_config, std::move(client));
    filter_->initializeReadFilterCallbacks(read_callbacks_);
    filter_->initializeWriteFilterCallbacks(write_callbacks_);
  }

  // Add dynamic metadata to the stream info
  void addDynamicMetadata(const std::string& namespace_key, const std::string& key,
                          const std::string& value) {
    auto& metadata = *stream_info_.metadata_.mutable_filter_metadata();
    Protobuf::Struct struct_obj;
    auto& fields = *struct_obj.mutable_fields();
    fields[key].set_string_value(value);
    metadata[namespace_key] = struct_obj;
  }

  // Add typed dynamic metadata to the stream info
  void addTypedDynamicMetadata(const std::string& namespace_key, const Protobuf::Any& typed_value) {
    stream_info_.metadata_.mutable_typed_filter_metadata()->insert({namespace_key, typed_value});
  }

  uint64_t getCounterValue(const std::string& name) {
    const auto counter = TestUtility::findCounter(store_, name);
    return counter != nullptr ? counter->value() : 0;
  }

protected:
  NiceMock<Stats::MockIsolatedStatsStore> store_;
  Stats::Scope& scope_{*store_.rootScope()};
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

  // Check counter for spurious messages
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.spurious_msgs_received"));
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

  // Verify read_data_sent counter incremented
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.read_data_sent"));
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.stream_msgs_sent"));

  // Create a message with neither read_data nor write_data
  auto response = std::make_unique<envoy::service::network_ext_proc::v3::ProcessingResponse>();

  // Ensure no data is injected into either filter chain
  EXPECT_CALL(read_callbacks_, injectReadDataToFilterChain(_, _)).Times(0);
  EXPECT_CALL(write_callbacks_, injectWriteDataToFilterChain(_, _)).Times(0);

  filter_->onReceiveMessage(std::move(response));

  // Verify we count empty responses
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.empty_response_received"));
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.stream_msgs_received"));
}

// Test openStream method when processing is already complete
TEST_F(NetworkExtProcFilterTest, OpenStreamAfterProcessingComplete) {
  // First, mark processing as complete
  filter_->onGrpcError(Grpc::Status::Internal, "test error");

  // Verify the failure counter was incremented
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.streams_grpc_error"));

  // Should not attempt to create a new stream
  EXPECT_CALL(*client_, start(_, _, _, _)).Times(0);

  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  // No new streams should be started
  EXPECT_EQ(0, getCounterValue("network_ext_proc.test_ext_proc.streams_started"));
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
  // Check failure counters
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.stream_open_failures"));
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
  // Verify stream open failure counter and connection closed counter
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.stream_open_failures"));
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.connections_closed"));
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

  // Stream should be started
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.streams_started"));

  // Now simulate a gRPC error
  // With failure_mode_allow=true, connection should NOT be closed
  EXPECT_CALL(connection_, close(_, _)).Times(0);

  filter_->onGrpcError(Grpc::Status::Internal, "test error");

  // Verify error counters
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.streams_grpc_error"));
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.failure_mode_allowed"));
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.streams_closed"));

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

  // Verify error counters
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.streams_grpc_error"));
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.streams_closed"));
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.connections_closed"));

  // Failure mode allowed should not be incremented
  EXPECT_EQ(0, getCounterValue("network_ext_proc.test_ext_proc.failure_mode_allowed"));
}

// Test normal processing flow for read data
TEST_F(NetworkExtProcFilterTest, NormalProcessingReadData) {
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

  // Check read data sent counter
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.read_data_sent"));
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.stream_msgs_sent"));

  // Simulate response from external processor
  envoy::service::network_ext_proc::v3::ProcessingResponse response;
  auto* read_data = response.mutable_read_data();
  read_data->set_data("modified");
  read_data->set_end_of_stream(false);

  // Expect data to be injected to the filter chain
  EXPECT_CALL(read_callbacks_, injectReadDataToFilterChain(_, false));

  filter_->onReceiveMessage(
      std::make_unique<envoy::service::network_ext_proc::v3::ProcessingResponse>(response));

  // Check data counters
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.read_data_injected"));
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.stream_msgs_received"));
}

// Test normal processing flow for write data
TEST_F(NetworkExtProcFilterTest, NormalProcessingWriteData) {
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
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onWrite(data, false));

  // Check write data sent counter
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.write_data_sent"));
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.stream_msgs_sent"));

  // Simulate response from external processor
  envoy::service::network_ext_proc::v3::ProcessingResponse response;
  auto* write_data = response.mutable_write_data();
  write_data->set_data("modified");
  write_data->set_end_of_stream(false);

  // Expect data to be injected to the filter chain
  EXPECT_CALL(write_callbacks_, injectWriteDataToFilterChain(_, false));

  filter_->onReceiveMessage(
      std::make_unique<envoy::service::network_ext_proc::v3::ProcessingResponse>(response));

  // Check data counters
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.write_data_injected"));
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.stream_msgs_received"));
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

  // Verify counters
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.streams_grpc_close"));
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.streams_closed"));

  // Subsequent data should pass through directly
  Buffer::OwnedImpl more_data("more");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(more_data, false));
}

// Test edge case with null stream in sendRequest
TEST_F(NetworkExtProcFilterTest, SendRequestWithNullStream) {
  // Set filter's stream to nullptr
  auto filter_config = std::make_shared<Config>(createConfig(false), scope_);
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

// Test downstream connection close event
TEST_F(NetworkExtProcFilterTest, DownstreamConnectionCloseEvent) {
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

  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  // Set up expectation for stream closure
  EXPECT_CALL(*stream_ptr, close()).WillOnce(Return(true));

  // Simulate downstream connection close
  Network::ConnectionEvent close_event = Network::ConnectionEvent::RemoteClose;
  filter_->onDownstreamEvent(close_event);

  // Verify stream close counter
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.streams_closed"));
}

// Test processing mode configurations
TEST_F(NetworkExtProcFilterTest, ProcessingModeConfigurations) {
  // Test with SKIP for read processing
  auto config = createConfig(false);
  config.mutable_processing_mode()->set_process_read(
      envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::SKIP);

  auto filter_config = std::make_shared<Config>(config, scope_);
  auto client = std::make_unique<NiceMock<MockExternalProcessorClient>>();
  client_ = client.get();
  filter_ = std::make_unique<NetworkExtProcFilter>(filter_config, std::move(client));
  filter_->initializeReadFilterCallbacks(read_callbacks_);
  filter_->initializeWriteFilterCallbacks(write_callbacks_);

  // With process_read set to SKIP, data should pass through directly
  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  // No stream should be created and no messages should be sent
  EXPECT_EQ(0, getCounterValue("network_ext_proc.test_ext_proc.streams_started"));
  EXPECT_EQ(0, getCounterValue("network_ext_proc.test_ext_proc.stream_msgs_sent"));

  // Testing SKIP for write processing
  config = createConfig(false);
  config.mutable_processing_mode()->set_process_write(
      envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::SKIP);

  filter_config = std::make_shared<Config>(config, scope_);
  client = std::make_unique<NiceMock<MockExternalProcessorClient>>();
  client_ = client.get();
  filter_ = std::make_unique<NetworkExtProcFilter>(filter_config, std::move(client));
  filter_->initializeReadFilterCallbacks(read_callbacks_);
  filter_->initializeWriteFilterCallbacks(write_callbacks_);

  // With process_write set to SKIP, data should pass through directly
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onWrite(data, false));

  // No stream should be created and no messages should be sent
  EXPECT_EQ(0, getCounterValue("network_ext_proc.test_ext_proc.streams_started"));
  EXPECT_EQ(0, getCounterValue("network_ext_proc.test_ext_proc.stream_msgs_sent"));
}

// Test metadata forwarding when no namespaces are configured
TEST_F(NetworkExtProcFilterTest, NoMetadataForwardingConfigured) {
  // Create a filter with no metadata options
  recreateFilterWithMetadataOptions({}, {});

  // Add some metadata to the stream info
  addDynamicMetadata("test-namespace", "key1", "value1");

  // Create a mock stream to verify request content
  auto stream = std::make_unique<NiceMock<MockExternalProcessorStream>>();
  auto* stream_ptr = stream.get();

  // This will capture the request that's sent to the external processor
  EXPECT_CALL(*stream_ptr, send(_, false))
      .WillOnce(
          testing::Invoke([](envoy::service::network_ext_proc::v3::ProcessingRequest&& request,
                             bool /*end_stream*/) {
            // Verify the request doesn't have metadata
            EXPECT_FALSE(request.has_metadata());
          }));

  EXPECT_CALL(*client_, start(_, _, _, _))
      .WillOnce(testing::Invoke(
          [&](ExternalProcessorCallbacks&, const Grpc::GrpcServiceConfigWithHashKey&,
              Http::AsyncClient::StreamOptions&,
              Http::StreamFilterSidestreamWatermarkCallbacks&) -> ExternalProcessorStreamPtr {
            return std::move(stream);
          }));

  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));
}

// Test untyped metadata forwarding
TEST_F(NetworkExtProcFilterTest, UntypedMetadataForwarding) {
  // Create a filter with untyped metadata forwarding
  recreateFilterWithMetadataOptions({"test-namespace"}, {});

  // Add metadata to the stream info
  addDynamicMetadata("test-namespace", "key1", "value1");
  addDynamicMetadata("other-namespace", "key2", "value2"); // Should not be forwarded

  // Create a mock stream to verify request content
  auto stream = std::make_unique<NiceMock<MockExternalProcessorStream>>();
  auto* stream_ptr = stream.get();

  // This will capture the request that's sent to the external processor
  EXPECT_CALL(*stream_ptr, send(_, false))
      .WillOnce(
          testing::Invoke([](envoy::service::network_ext_proc::v3::ProcessingRequest&& request,
                             bool /*end_stream*/) {
            // Verify the request has metadata
            EXPECT_TRUE(request.has_metadata());

            // Verify it has the test-namespace but not other-namespace
            const auto& metadata = request.metadata().filter_metadata();
            EXPECT_TRUE(metadata.contains("test-namespace"));
            EXPECT_FALSE(metadata.contains("other-namespace"));

            // Verify the key-value pairs within test-namespace
            const auto& test_ns = metadata.at("test-namespace");
            EXPECT_TRUE(test_ns.fields().contains("key1"));
            EXPECT_EQ(test_ns.fields().at("key1").string_value(), "value1");
          }));

  EXPECT_CALL(*client_, start(_, _, _, _))
      .WillOnce(testing::Invoke(
          [&](ExternalProcessorCallbacks&, const Grpc::GrpcServiceConfigWithHashKey&,
              Http::AsyncClient::StreamOptions&,
              Http::StreamFilterSidestreamWatermarkCallbacks&) -> ExternalProcessorStreamPtr {
            return std::move(stream);
          }));

  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));
}

// Test typed metadata forwarding
TEST_F(NetworkExtProcFilterTest, TypedMetadataForwarding) {
  // Create a filter with typed metadata forwarding
  recreateFilterWithMetadataOptions({}, {"typed-namespace"});

  // Create a typed metadata value
  Protobuf::Any typed_value;
  typed_value.set_type_url("type.googleapis.com/envoy.test.TestMessage");
  typed_value.set_value("test-value");

  // Add typed metadata to the stream info
  addTypedDynamicMetadata("typed-namespace", typed_value);

  // Create another typed value that shouldn't be forwarded
  Protobuf::Any other_typed_value;
  other_typed_value.set_type_url("type.googleapis.com/envoy.test.OtherMessage");
  other_typed_value.set_value("other-value");
  addTypedDynamicMetadata("other-namespace", other_typed_value);

  // Create a mock stream to verify request content
  auto stream = std::make_unique<NiceMock<MockExternalProcessorStream>>();
  auto* stream_ptr = stream.get();

  // This will capture the request that's sent to the external processor
  EXPECT_CALL(*stream_ptr, send(_, false))
      .WillOnce(testing::Invoke(
          [&typed_value](envoy::service::network_ext_proc::v3::ProcessingRequest&& request,
                         bool /*end_stream*/) {
            // Verify the request has metadata
            EXPECT_TRUE(request.has_metadata());

            // Verify it has the typed-namespace but not other-namespace
            const auto& typed_metadata = request.metadata().typed_filter_metadata();
            EXPECT_TRUE(typed_metadata.contains("typed-namespace"));
            EXPECT_FALSE(typed_metadata.contains("other-namespace"));

            // Verify the typed value matches what we set
            const auto& actual_typed_value = typed_metadata.at("typed-namespace");
            EXPECT_EQ(actual_typed_value.type_url(), typed_value.type_url());
            EXPECT_EQ(actual_typed_value.value(), typed_value.value());
          }));

  EXPECT_CALL(*client_, start(_, _, _, _))
      .WillOnce(testing::Invoke(
          [&](ExternalProcessorCallbacks&, const Grpc::GrpcServiceConfigWithHashKey&,
              Http::AsyncClient::StreamOptions&,
              Http::StreamFilterSidestreamWatermarkCallbacks&) -> ExternalProcessorStreamPtr {
            return std::move(stream);
          }));

  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));
}

// Test both untyped and typed metadata forwarding together
TEST_F(NetworkExtProcFilterTest, BothTypedAndUntypedMetadataForwarding) {
  // Create a filter that forwards both typed and untyped metadata
  recreateFilterWithMetadataOptions({"untyped-ns"}, {"typed-ns"});

  // Add untyped metadata
  addDynamicMetadata("untyped-ns", "key1", "value1");

  // Add typed metadata
  Protobuf::Any typed_value;
  typed_value.set_type_url("type.googleapis.com/envoy.test.TestMessage");
  typed_value.set_value("test-value");
  addTypedDynamicMetadata("typed-ns", typed_value);

  // Create a mock stream to verify request content
  auto stream = std::make_unique<NiceMock<MockExternalProcessorStream>>();
  auto* stream_ptr = stream.get();

  // This will capture the request that's sent to the external processor
  EXPECT_CALL(*stream_ptr, send(_, false))
      .WillOnce(testing::Invoke(
          [&typed_value](envoy::service::network_ext_proc::v3::ProcessingRequest&& request,
                         bool /*end_stream*/) {
            // Verify the request has metadata
            EXPECT_TRUE(request.has_metadata());

            // Verify untyped metadata
            const auto& filter_metadata = request.metadata().filter_metadata();
            EXPECT_TRUE(filter_metadata.contains("untyped-ns"));
            const auto& untyped_ns = filter_metadata.at("untyped-ns");
            EXPECT_TRUE(untyped_ns.fields().contains("key1"));
            EXPECT_EQ(untyped_ns.fields().at("key1").string_value(), "value1");

            // Verify typed metadata
            const auto& typed_metadata = request.metadata().typed_filter_metadata();
            EXPECT_TRUE(typed_metadata.contains("typed-ns"));
            const auto& actual_typed_value = typed_metadata.at("typed-ns");
            EXPECT_EQ(actual_typed_value.type_url(), typed_value.type_url());
            EXPECT_EQ(actual_typed_value.value(), typed_value.value());
          }));

  EXPECT_CALL(*client_, start(_, _, _, _))
      .WillOnce(testing::Invoke(
          [&](ExternalProcessorCallbacks&, const Grpc::GrpcServiceConfigWithHashKey&,
              Http::AsyncClient::StreamOptions&,
              Http::StreamFilterSidestreamWatermarkCallbacks&) -> ExternalProcessorStreamPtr {
            return std::move(stream);
          }));

  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));
}

// Test metadata forwarding with empty metadata
TEST_F(NetworkExtProcFilterTest, MetadataForwardingWithEmptyMetadata) {
  // Create a filter with metadata options but don't add any metadata
  recreateFilterWithMetadataOptions({"untyped-ns"}, {"typed-ns"});

  // Create a mock stream to verify request content
  auto stream = std::make_unique<NiceMock<MockExternalProcessorStream>>();
  auto* stream_ptr = stream.get();

  // This will capture the request that's sent to the external processor
  EXPECT_CALL(*stream_ptr, send(_, false))
      .WillOnce(
          testing::Invoke([](envoy::service::network_ext_proc::v3::ProcessingRequest&& request,
                             bool /*end_stream*/) {
            // Verify the request doesn't have metadata since no matching metadata exists
            EXPECT_FALSE(request.has_metadata());
          }));

  EXPECT_CALL(*client_, start(_, _, _, _))
      .WillOnce(testing::Invoke(
          [&](ExternalProcessorCallbacks&, const Grpc::GrpcServiceConfigWithHashKey&,
              Http::AsyncClient::StreamOptions&,
              Http::StreamFilterSidestreamWatermarkCallbacks&) -> ExternalProcessorStreamPtr {
            return std::move(stream);
          }));

  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));
}

// Test timeout configuration
TEST_F(NetworkExtProcFilterTest, TimeoutConfiguration) {
  // Test default timeout value
  auto config = createConfig(false);
  auto filter_config = std::make_shared<Config>(config, scope_);
  EXPECT_EQ(filter_config->messageTimeout().count(), 200);

  // Test custom timeout value
  config.mutable_message_timeout()->set_seconds(2);
  filter_config = std::make_shared<Config>(config, scope_);
  EXPECT_EQ(filter_config->messageTimeout().count(), 2000);

  // Test zero timeout (means no timeout)
  config.mutable_message_timeout()->set_seconds(0);
  config.mutable_message_timeout()->set_nanos(0);
  filter_config = std::make_shared<Config>(config, scope_);
  EXPECT_EQ(filter_config->messageTimeout().count(), 0);
}

// Test message timeout with failure_mode_allow=true
TEST_F(NetworkExtProcFilterTest, MessageTimeoutWithFailureModeAllow) {
  // Create filter with failure_mode_allow=true and custom timeout
  auto config = createConfig(true);
  config.mutable_message_timeout()->set_nanos(100000000); // 100ms
  auto filter_config = std::make_shared<Config>(config, scope_);
  auto client = std::make_unique<NiceMock<MockExternalProcessorClient>>();
  client_ = client.get();
  filter_ = std::make_unique<NetworkExtProcFilter>(filter_config, std::move(client));
  filter_->initializeReadFilterCallbacks(read_callbacks_);
  filter_->initializeWriteFilterCallbacks(write_callbacks_);

  // Create a mock stream
  auto stream = std::make_unique<NiceMock<MockExternalProcessorStream>>();
  auto* stream_ptr = stream.get();

  EXPECT_CALL(*stream_ptr, send(_, false));
  EXPECT_CALL(*client_, start(_, _, _, _))
      .WillOnce([&](ExternalProcessorCallbacks&, const Grpc::GrpcServiceConfigWithHashKey&,
                    Http::AsyncClient::StreamOptions&,
                    Http::StreamFilterSidestreamWatermarkCallbacks&) -> ExternalProcessorStreamPtr {
        return std::move(stream);
      });

  // Send data which starts the timer
  EXPECT_CALL(read_callbacks_, disableClose(true));
  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  // Simulate timeout - with failure_mode_allow=true, connection should NOT close
  EXPECT_CALL(read_callbacks_, disableClose(false));
  EXPECT_CALL(*stream_ptr, close()).WillOnce(Return(true));
  EXPECT_CALL(connection_, close(_, _)).Times(0); // Should NOT close connection

  filter_->handleMessageTimeout(true); // Read timeout

  // Verify timeout counters
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.message_timeouts"));
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.streams_closed"));
  EXPECT_EQ(0, getCounterValue("network_ext_proc.test_ext_proc.connections_closed"));
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.failure_mode_allowed"));

  // Subsequent data should pass through
  Buffer::OwnedImpl more_data("more");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(more_data, false));
}

// Test timeout during write operation
TEST_F(NetworkExtProcFilterTest, WriteMessageTimeout) {
  auto config = createConfig(false);
  config.mutable_message_timeout()->set_nanos(100000000); // 100ms
  auto filter_config = std::make_shared<Config>(config, scope_);
  auto client = std::make_unique<NiceMock<MockExternalProcessorClient>>();
  client_ = client.get();
  filter_ = std::make_unique<NetworkExtProcFilter>(filter_config, std::move(client));
  filter_->initializeReadFilterCallbacks(read_callbacks_);
  filter_->initializeWriteFilterCallbacks(write_callbacks_);

  // Create a mock stream
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

  // Send write data which starts the timer
  EXPECT_CALL(write_callbacks_, disableClose(true));
  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onWrite(data, false));

  // Simulate timeout
  EXPECT_CALL(write_callbacks_, disableClose(false)).Times(2);
  EXPECT_CALL(*stream_ptr, close()).WillOnce(Return(true));
  EXPECT_CALL(connection_,
              close(Network::ConnectionCloseType::FlushWrite, "ext_proc_message_timeout"))
      .WillOnce([]() {});

  filter_->handleMessageTimeout(false);

  // Verify counters
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.message_timeouts"));
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.write_data_sent"));
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.connections_closed"));
}

// Test timeout with both read and write pending
TEST_F(NetworkExtProcFilterTest, TimeoutWithBothOperationsPending) {
  auto config = createConfig(false);
  config.mutable_message_timeout()->set_nanos(100000000); // 100ms
  auto filter_config = std::make_shared<Config>(config, scope_);
  auto client = std::make_unique<NiceMock<MockExternalProcessorClient>>();
  client_ = client.get();
  filter_ = std::make_unique<NetworkExtProcFilter>(filter_config, std::move(client));
  filter_->initializeReadFilterCallbacks(read_callbacks_);
  filter_->initializeWriteFilterCallbacks(write_callbacks_);

  // Create a mock stream
  auto stream = std::make_unique<NiceMock<MockExternalProcessorStream>>();
  auto* stream_ptr = stream.get();

  EXPECT_CALL(*stream_ptr, send(_, false)).Times(2); // Both read and write
  EXPECT_CALL(*client_, start(_, _, _, _))
      .WillOnce(testing::Invoke(
          [&](ExternalProcessorCallbacks&, const Grpc::GrpcServiceConfigWithHashKey&,
              Http::AsyncClient::StreamOptions&,
              Http::StreamFilterSidestreamWatermarkCallbacks&) -> ExternalProcessorStreamPtr {
            return std::move(stream);
          }));

  // Send both read and write data
  EXPECT_CALL(read_callbacks_, disableClose(true));
  Buffer::OwnedImpl read_data("read_test");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(read_data, false));

  EXPECT_CALL(write_callbacks_, disableClose(true));
  Buffer::OwnedImpl write_data("write_test");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onWrite(write_data, false));

  // Simulate timeout - should clean up both directions
  EXPECT_CALL(read_callbacks_, disableClose(false)).Times(2);
  EXPECT_CALL(write_callbacks_, disableClose(false)).Times(2);
  EXPECT_CALL(*stream_ptr, close()).WillOnce(Return(true));
  EXPECT_CALL(connection_,
              close(Network::ConnectionCloseType::FlushWrite, "ext_proc_message_timeout"))
      .WillOnce([]() {});

  filter_->handleMessageTimeout(true); // Timeout on read, but should clean up both

  // Verify both operations were cleaned up
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.message_timeouts"));
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.connections_closed"));
}

// Test that timer stops when response is received
TEST_F(NetworkExtProcFilterTest, TimerStopsOnResponse) {
  auto config = createConfig(false);
  config.mutable_message_timeout()->set_nanos(100000000); // 100ms
  auto filter_config = std::make_shared<Config>(config, scope_);
  auto client = std::make_unique<NiceMock<MockExternalProcessorClient>>();
  client_ = client.get();
  filter_ = std::make_unique<NetworkExtProcFilter>(filter_config, std::move(client));
  filter_->initializeReadFilterCallbacks(read_callbacks_);
  filter_->initializeWriteFilterCallbacks(write_callbacks_);

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

  // Send data which starts the timer
  EXPECT_CALL(read_callbacks_, disableClose(true));
  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  // Receive response before timeout - timer should be stopped
  envoy::service::network_ext_proc::v3::ProcessingResponse response;
  auto* read_data = response.mutable_read_data();
  read_data->set_data("modified");
  read_data->set_end_of_stream(false);

  EXPECT_CALL(read_callbacks_, injectReadDataToFilterChain(_, false));
  EXPECT_CALL(read_callbacks_, disableClose(false));

  filter_->onReceiveMessage(
      std::make_unique<envoy::service::network_ext_proc::v3::ProcessingResponse>(response));

  // No timeout should occur
  EXPECT_EQ(0, getCounterValue("network_ext_proc.test_ext_proc.message_timeouts"));
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.read_data_injected"));
}

// Test timeout cleanup on stream errors
TEST_F(NetworkExtProcFilterTest, TimeoutCleanupOnGrpcError) {
  auto config = createConfig(false);
  config.mutable_message_timeout()->set_nanos(100000000); // 100ms
  auto filter_config = std::make_shared<Config>(config, scope_);
  auto client = std::make_unique<NiceMock<MockExternalProcessorClient>>();
  client_ = client.get();
  filter_ = std::make_unique<NetworkExtProcFilter>(filter_config, std::move(client));
  filter_->initializeReadFilterCallbacks(read_callbacks_);
  filter_->initializeWriteFilterCallbacks(write_callbacks_);

  // Create a mock stream
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

  // Send data which starts the timer
  EXPECT_CALL(read_callbacks_, disableClose(true));
  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  // Simulate gRPC error - should stop timer and clean up
  EXPECT_CALL(read_callbacks_, disableClose(false)); // Expect re-enable before close
  EXPECT_CALL(*stream_ptr, close()).WillOnce(Return(true));
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::FlushWrite, "ext_proc_grpc_error"))
      .WillOnce([]() {});

  filter_->onGrpcError(Grpc::Status::Internal, "test error");

  // Verify cleanup but no timeout counter
  EXPECT_EQ(0, getCounterValue("network_ext_proc.test_ext_proc.message_timeouts"));
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.streams_grpc_error"));
  EXPECT_EQ(1, getCounterValue("network_ext_proc.test_ext_proc.connections_closed"));
}

// Test zero timeout (disabled)
TEST_F(NetworkExtProcFilterTest, ZeroTimeoutDisabled) {
  auto config = createConfig(false);
  config.mutable_message_timeout()->set_nanos(0);
  auto filter_config = std::make_shared<Config>(config, scope_);
  auto client = std::make_unique<NiceMock<MockExternalProcessorClient>>();
  client_ = client.get();
  filter_ = std::make_unique<NetworkExtProcFilter>(filter_config, std::move(client));
  filter_->initializeReadFilterCallbacks(read_callbacks_);
  filter_->initializeWriteFilterCallbacks(write_callbacks_);

  EXPECT_EQ(filter_->getMessageTimeout().count(), 0);

  // With zero timeout, timer should not be started
  auto stream = std::make_unique<NiceMock<MockExternalProcessorStream>>();
  EXPECT_CALL(*stream, send(_, false));
  EXPECT_CALL(*client_, start(_, _, _, _))
      .WillOnce(testing::Invoke(
          [&](ExternalProcessorCallbacks&, const Grpc::GrpcServiceConfigWithHashKey&,
              Http::AsyncClient::StreamOptions&,
              Http::StreamFilterSidestreamWatermarkCallbacks&) -> ExternalProcessorStreamPtr {
            return std::move(stream);
          }));

  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  // No timeout should occur with zero timeout
  EXPECT_EQ(0, getCounterValue("network_ext_proc.test_ext_proc.message_timeouts"));
}

// Test NetworkExtProcLoggingInfo basic operations.
TEST(NetworkExtProcLoggingInfoTest, BasicOperations) {
  NetworkExtProcLoggingInfo logging_info;

  // Test recording gRPC calls for read direction
  logging_info.recordGrpcCall(std::chrono::microseconds(100), Grpc::Status::WellKnownGrpcStatus::Ok,
                              true);
  logging_info.recordGrpcCall(std::chrono::microseconds(200),
                              Grpc::Status::WellKnownGrpcStatus::Unavailable, true);

  const auto& read_stats = logging_info.readStats();
  EXPECT_EQ(read_stats.grpc_calls_, 2);
  EXPECT_EQ(read_stats.grpc_errors_, 1);
  EXPECT_EQ(read_stats.total_latency_.count(), 300);
  EXPECT_EQ(read_stats.max_latency_.count(), 200);
  EXPECT_EQ(read_stats.min_latency_.count(), 100);
  EXPECT_EQ(logging_info.lastCallStatus(), Grpc::Status::WellKnownGrpcStatus::Unavailable);

  // Test recording gRPC calls for write direction
  logging_info.recordGrpcCall(std::chrono::microseconds(50), Grpc::Status::WellKnownGrpcStatus::Ok,
                              false);

  const auto& write_stats = logging_info.writeStats();
  EXPECT_EQ(write_stats.grpc_calls_, 1);
  EXPECT_EQ(write_stats.grpc_errors_, 0);
  EXPECT_EQ(write_stats.total_latency_.count(), 50);
  EXPECT_EQ(write_stats.max_latency_.count(), 50);
  EXPECT_EQ(write_stats.min_latency_.count(), 50);
}

// Test NetworkExtProcLoggingInfo bytes processing count.
TEST(NetworkExtProcLoggingInfoTest, BytesProcessing) {
  NetworkExtProcLoggingInfo logging_info;

  // Add bytes for read direction
  logging_info.addBytesProcessed(100, true);
  logging_info.addBytesProcessed(200, true);
  logging_info.addBytesProcessed(50, true);

  // Add bytes for write direction
  logging_info.addBytesProcessed(150, false);
  logging_info.addBytesProcessed(250, false);

  EXPECT_EQ(logging_info.readStats().bytes_processed_, 350);
  EXPECT_EQ(logging_info.readStats().message_count_, 3);
  EXPECT_EQ(logging_info.writeStats().bytes_processed_, 400);
  EXPECT_EQ(logging_info.writeStats().message_count_, 2);
  EXPECT_EQ(logging_info.totalBytesProcessed(), 750);
}

// Test logging info for connection info.
TEST(NetworkExtProcLoggingInfoTest, ConnectionInfoSetup) {
  NetworkExtProcLoggingInfo logging_info;

  NiceMock<Network::MockConnection> connection;
  Network::ConnectionInfoSetterImpl connection_info(nullptr, nullptr);

  auto local_address = Network::Utility::parseInternetAddressNoThrow("192.168.1.1", 9090);
  auto remote_address = Network::Utility::parseInternetAddressNoThrow("10.0.0.5", 54321);
  connection_info.setLocalAddress(local_address);
  connection_info.setRemoteAddress(remote_address);

  EXPECT_CALL(connection, connectionInfoProvider()).WillRepeatedly(ReturnRef(connection_info));
  logging_info.setConnectionInfo(&connection);

  EXPECT_EQ(logging_info.peerAddress(), "10.0.0.5:54321");
  EXPECT_EQ(logging_info.localAddress(), "192.168.1.1:9090");
}

// Test gRPC call latency recording
TEST_F(NetworkExtProcFilterTest, LoggingInfoLatencyTracking) {
  recreateFilterWithConfig(false);

  auto stream = std::make_unique<NiceMock<MockExternalProcessorStream>>();
  auto* stream_ptr = stream.get();

  EXPECT_CALL(*stream_ptr, send(_, false)).Times(3);
  EXPECT_CALL(*client_, start(_, _, _, _))
      .WillOnce([&](ExternalProcessorCallbacks&, const Grpc::GrpcServiceConfigWithHashKey&,
                    Http::AsyncClient::StreamOptions&,
                    Http::StreamFilterSidestreamWatermarkCallbacks&) -> ExternalProcessorStreamPtr {
        return std::move(stream);
      });

  // Start processing
  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));
  auto response = std::make_unique<envoy::service::network_ext_proc::v3::ProcessingResponse>();
  response->mutable_read_data()->set_data("modified");
  connection_.dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(100));
  filter_->onReceiveMessage(std::move(response));

  Buffer::OwnedImpl data_second("test_second");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data_second, false));
  auto response_second =
      std::make_unique<envoy::service::network_ext_proc::v3::ProcessingResponse>();
  response_second->mutable_read_data()->set_data("modified");
  connection_.dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(200));
  filter_->onReceiveMessage(std::move(response_second));

  Buffer::OwnedImpl write_data("write");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onWrite(write_data, false));
  auto write_response =
      std::make_unique<envoy::service::network_ext_proc::v3::ProcessingResponse>();
  write_response->mutable_write_data()->set_data("write");
  connection_.dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(50));
  filter_->onReceiveMessage(std::move(write_response));

  auto& filter_state = read_callbacks_.connection().streamInfo().filterState();
  auto logging_info =
      filter_state->getDataReadOnly<NetworkExtProcLoggingInfo>("envoy.filters.network.ext_proc");

  EXPECT_EQ(logging_info->readStats().grpc_calls_, 2);
  EXPECT_EQ(logging_info->readStats().total_latency_.count(), 300000);
  EXPECT_EQ(logging_info->readStats().max_latency_.count(), 200000);
  EXPECT_EQ(logging_info->readStats().min_latency_.count(), 100000);
  EXPECT_EQ(logging_info->lastCallStatus(), Grpc::Status::WellKnownGrpcStatus::Ok);

  EXPECT_EQ(logging_info->writeStats().grpc_calls_, 1);
  EXPECT_EQ(logging_info->writeStats().total_latency_.count(), 50000);
}

// Test gRPC call onGrpcError recording
TEST_F(NetworkExtProcFilterTest, LoggingInfoOnError) {
  recreateFilterWithConfig(true);

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
  connection_.dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(100));
  filter_->onGrpcError(Grpc::Status::WellKnownGrpcStatus::ResourceExhausted, "test error");

  auto& filter_state = read_callbacks_.connection().streamInfo().filterState();
  auto logging_info =
      filter_state->getDataReadOnly<NetworkExtProcLoggingInfo>("envoy.filters.network.ext_proc");

  EXPECT_EQ(logging_info->lastCallStatus(), Grpc::Status::WellKnownGrpcStatus::ResourceExhausted);
}

} // namespace
} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
