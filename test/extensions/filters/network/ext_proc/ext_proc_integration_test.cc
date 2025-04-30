#include <string>

#include "envoy/http/codec.h"
#include "envoy/service/network_ext_proc/v3/network_external_processor.pb.h"

#include "source/extensions/filters/network/ext_proc/config.h"

#include "test/config/utility.h"
#include "test/integration/base_integration_test.h"
#include "test/integration/fake_upstream.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtProc {

using envoy::service::network_ext_proc::v3::ProcessingRequest;
using envoy::service::network_ext_proc::v3::ProcessingResponse;

class NetworkExtProcFilterIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public BaseIntegrationTest {
public:
  NetworkExtProcFilterIntegrationTest()
      : BaseIntegrationTest(GetParam(), absl::StrCat(Envoy::ConfigHelper::baseConfig(), R"EOF(
    filter_chains:
      filters:
        - name: envoy.network_ext_proc.ext_proc_filter
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.ext_proc.v3.NetworkExternalProcessor
            grpc_service:
              envoy_grpc:
                cluster_name: "cluster_1"
        - name: envoy.filters.network.tcp_proxy
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
            stat_prefix: tcpproxy_stats
            cluster: cluster_0
)EOF")) {
    enableHalfClose(true);
    scoped_runtime_.mergeValues(
        {{"envoy.reloadable_features.connection_close_through_filter_manager", "true"}});
  }

  void createUpstreams() override {
    BaseIntegrationTest::createUpstreams();
    grpc_upstream_ = &addFakeUpstream(Http::CodecType::HTTP2);
  }

  void initializeWithModifier(
      std::function<
          void(envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor&)>
          config_modifier = nullptr) {
    config_helper_.renameListener("network_ext_proc_filter");
    config_helper_.addConfigModifier(
        [&, config_modifier](::envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          // Common cluster setup
          auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
          cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
          cluster->set_name("cluster_1");
          cluster->mutable_load_assignment()->set_cluster_name("cluster_1");
          Envoy::ConfigHelper::setHttp2WithMaxConcurrentStreams(
              *(bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(1)), 2);

          // Apply config modifier if provided
          if (config_modifier) {
            // Find the network filter
            auto* listeners = bootstrap.mutable_static_resources()->mutable_listeners(0);
            auto* filter_chain = listeners->mutable_filter_chains(0);
            auto* filters = filter_chain->mutable_filters();
            for (int i = 0; i < filters->size(); i++) {
              if ((*filters)[i].name() == "envoy.network_ext_proc.ext_proc_filter") {
                envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor config;
                (*filters)[i].mutable_typed_config()->UnpackTo(&config);

                // Apply the provided modifier function
                config_modifier(config);

                (*filters)[i].mutable_typed_config()->PackFrom(config);
                break;
              }
            }
          }
        });
    BaseIntegrationTest::initialize();
  }

  void initialize() override { initializeWithModifier(); }

  void initializeWithFailureModeAllow() {
    initializeWithModifier([](auto& config) { config.set_failure_mode_allow(true); });
  }

  void initializeWithSkipProcessingModes(bool skip_read) {
    initializeWithModifier([skip_read](auto& config) {
      auto* processing_mode = config.mutable_processing_mode();
      if (skip_read) {
        processing_mode->set_process_read(
            envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::SKIP);
      } else {
        processing_mode->set_process_write(
            envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::SKIP);
      }
    });
  }

  void waitForFirstGrpcMessage(ProcessingRequest& request) {
    ENVOY_LOG_MISC(debug, "waitForFirstGrpcMessage {}", grpc_upstream_->localAddress()->asString());
    ASSERT_TRUE(grpc_upstream_->waitForHttpConnection(*dispatcher_, processor_connection_));
    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
  }

  void sendReadGrpcMessage(std::string data, bool end_stream, bool first_message = false) {
    ProcessingResponse response;
    auto* read_data = response.mutable_read_data();
    read_data->set_data(data);
    read_data->set_end_of_stream(end_stream);
    ENVOY_LOG_MISC(debug, "Sending READ gRPC message: {}", response.DebugString());
    if (first_message) {
      processor_stream_->startGrpcStream();
    }
    processor_stream_->sendGrpcMessage(response);
  }

  void sendReadGrpcMessageWithStream(Envoy::FakeStreamPtr& stream, std::string data,
                                     bool end_stream, bool first_message = false) {
    ProcessingResponse response;
    auto* read_data = response.mutable_read_data();
    read_data->set_data(data);
    read_data->set_end_of_stream(end_stream);
    ENVOY_LOG_MISC(debug, "Sending READ gRPC message via stream: {}", response.DebugString());
    if (first_message) {
      stream->startGrpcStream();
    }
    stream->sendGrpcMessage(response);
  }

  void sendWriteGrpcMessage(std::string data, bool end_stream, bool first_message = false) {
    ProcessingResponse response;
    auto* write_data = response.mutable_write_data();
    write_data->set_data(data);
    write_data->set_end_of_stream(end_stream);
    ENVOY_LOG_MISC(debug, "Sending WRITE gRPC message: {}", response.DebugString());
    if (first_message) {
      processor_stream_->startGrpcStream();
    }
    processor_stream_->sendGrpcMessage(response);
  }

  void sendWriteGrpcMessageWithStream(Envoy::FakeStreamPtr& stream, std::string data,
                                      bool end_stream, bool first_message = false) {
    ProcessingResponse response;
    auto* write_data = response.mutable_write_data();
    write_data->set_data(data);
    write_data->set_end_of_stream(end_stream);
    ENVOY_LOG_MISC(debug, "Sending WRITE gRPC message via stream: {}", response.DebugString());
    if (first_message) {
      stream->startGrpcStream();
    }
    stream->sendGrpcMessage(response);
  }

  void closeGrpcStream() {
    if (processor_stream_) {
      processor_stream_->encodeHeaders(
          Http::TestResponseHeaderMapImpl{{":status", "500"}, {"grpc-status", "8"}, {"foo", "bar"}},
          true);
    }
  }

  void TearDown() override {
    if (processor_connection_) {
      ASSERT_TRUE(processor_connection_->close());
      ASSERT_TRUE(processor_connection_->waitForDisconnect());
    }
  }

  Envoy::FakeUpstream* grpc_upstream_;
  Envoy::FakeHttpConnectionPtr processor_connection_;
  Envoy::FakeStreamPtr processor_stream_;
  TestScopedRuntime scoped_runtime_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, NetworkExtProcFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test downstream connection closing
TEST_P(NetworkExtProcFilterIntegrationTest, TcpProxyDownstreamClose) {
  initialize();
  Envoy::IntegrationTcpClientPtr tcp_client =
      makeTcpConnection(lookupPort("network_ext_proc_filter"));

  Envoy::FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(tcp_client->write("client_data", true));

  ProcessingRequest request;
  waitForFirstGrpcMessage(request);
  EXPECT_EQ(request.has_read_data(), true);
  EXPECT_EQ(request.read_data().data(), "client_data");

  sendReadGrpcMessage("client_data_inspected", true, true);

  ASSERT_TRUE(fake_upstream_connection->waitForData(21));
  // The server should see the half-close
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());

  tcp_client->close();
}

TEST_P(NetworkExtProcFilterIntegrationTest, MultipleClientConnections) {
  initialize();

  // max_concurrent_streams of the side stream cluster that is connected
  // to the gRPC server is 2.
  Envoy::IntegrationTcpClientPtr tcp_client_1 =
      makeTcpConnection(lookupPort("network_ext_proc_filter"));
  Envoy::FakeRawConnectionPtr fake_upstream_connection_1;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_1));

  Envoy::IntegrationTcpClientPtr tcp_client_2 =
      makeTcpConnection(lookupPort("network_ext_proc_filter"));
  Envoy::FakeRawConnectionPtr fake_upstream_connection_2;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_2));

  Envoy::IntegrationTcpClientPtr tcp_client_3 =
      makeTcpConnection(lookupPort("network_ext_proc_filter"));
  Envoy::FakeRawConnectionPtr fake_upstream_connection_3;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_3));

  ASSERT_TRUE(tcp_client_1->write("client_data_1", true));
  ProcessingRequest request_1;
  ASSERT_TRUE(grpc_upstream_->waitForHttpConnection(*dispatcher_, processor_connection_));
  ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request_1));

  sendReadGrpcMessageWithStream(processor_stream_, "client_data_1_inspected", true, true);
  ASSERT_TRUE(fake_upstream_connection_1->waitForData(23));

  ASSERT_TRUE(tcp_client_2->write("client_data_2", true));
  Envoy::FakeStreamPtr processor_stream_2;

  ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_2));
  ProcessingRequest request_2;
  ASSERT_TRUE(processor_stream_2->waitForGrpcMessage(*dispatcher_, request_2));
  sendReadGrpcMessageWithStream(processor_stream_2, "client_data_2_inspected", true, true);

  ASSERT_TRUE(fake_upstream_connection_2->waitForData(23));

  ASSERT_TRUE(tcp_client_3->write("client_data_3", true));
  // Max concurrent streams is 2, so a second connection will be created.
  Envoy::FakeHttpConnectionPtr processor_connection_2;
  ASSERT_TRUE(grpc_upstream_->waitForHttpConnection(*dispatcher_, processor_connection_2));
  Envoy::FakeStreamPtr processor_stream_3;
  ASSERT_TRUE(processor_connection_2->waitForNewStream(*dispatcher_, processor_stream_3));
  ProcessingRequest request_3;
  ASSERT_TRUE(processor_stream_3->waitForGrpcMessage(*dispatcher_, request_3));
  sendReadGrpcMessageWithStream(processor_stream_3, "client_data_3_inspected", true, true);
  ASSERT_TRUE(fake_upstream_connection_3->waitForData(23));

  tcp_client_1->close();
  tcp_client_2->close();
  ASSERT_TRUE(processor_connection_->close());
  ASSERT_TRUE(processor_connection_->waitForDisconnect());
  tcp_client_3->close();
  ASSERT_TRUE(processor_connection_2->close());
  ASSERT_TRUE(processor_connection_2->waitForDisconnect());
}

// Test that data is passed through both directions without data loss.
TEST_P(NetworkExtProcFilterIntegrationTest, TcpProxyUpstreamHalfCloseBothWays) {
  initialize();

  // Connect to the server
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc_filter"));

  // Write data from client to server
  ASSERT_TRUE(tcp_client->write("client_data", false));

  // Wait for the upstream connection and verify data
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ProcessingRequest request;
  waitForFirstGrpcMessage(request);
  EXPECT_EQ(request.has_read_data(), true);
  EXPECT_EQ(request.read_data().data(), "client_data");
  EXPECT_EQ(request.read_data().end_of_stream(), false);

  sendReadGrpcMessage("client_data_inspected", true, true);

  ASSERT_TRUE(fake_upstream_connection->waitForData(21));

  // Write data from server to client
  ASSERT_TRUE(fake_upstream_connection->write("server_response", true));

  ProcessingRequest write_request;
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, write_request));
  EXPECT_EQ(write_request.has_write_data(), true);
  EXPECT_EQ(write_request.write_data().data(), "server_response");
  EXPECT_EQ(write_request.write_data().end_of_stream(), true);

  sendWriteGrpcMessage("server_data_inspected", true);

  tcp_client->waitForData("server_data_inspected");

  // Close everything
  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(processor_stream_->waitForEndStream(*dispatcher_));
  tcp_client->close();
}

// Test that data is passed through both directions without data loss.
TEST_P(NetworkExtProcFilterIntegrationTest, TcpProxyDownstreamHalfCloseBothWays) {
  initialize();

  Envoy::IntegrationTcpClientPtr tcp_client =
      makeTcpConnection(lookupPort("network_ext_proc_filter"));

  Envoy::FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(fake_upstream_connection->write("server_data", true));

  ProcessingRequest request;
  waitForFirstGrpcMessage(request);
  EXPECT_EQ(request.has_write_data(), true);
  EXPECT_EQ(request.write_data().data(), "server_data");
  EXPECT_EQ(request.write_data().end_of_stream(), true);

  sendWriteGrpcMessage("server_data_inspected", true, true);

  tcp_client->waitForData("server_data_inspected");

  // Use true here, and listener connection will get remote close.
  // and the disableClose(true) will take effect to delay the deletion of the filter chain.
  ASSERT_TRUE(tcp_client->write("client_data", true));
  ProcessingRequest write_request;
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, write_request));
  EXPECT_EQ(write_request.has_read_data(), true);
  EXPECT_EQ(write_request.read_data().data(), "client_data");
  EXPECT_EQ(write_request.read_data().end_of_stream(), true);

  sendReadGrpcMessage("client_data_inspected", true);

  ASSERT_TRUE(fake_upstream_connection->waitForData(21));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->close();
}

// Test behavior when the gRPC stream fails
TEST_P(NetworkExtProcFilterIntegrationTest, GrpcStreamFailure) {
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc_filter"));
  ASSERT_TRUE(tcp_client->write("client_data", false));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ProcessingRequest request;
  waitForFirstGrpcMessage(request);

  // Close gRPC stream with an error status instead of Ok
  // This should trigger onGrpcError instead of onGrpcClose
  closeGrpcStream();
  ASSERT_TRUE(tcp_client->write("data", true));
  tcp_client->waitForDisconnect();
}

// Test behavior when failure_mode_allow is set to true and gRPC stream fails
TEST_P(NetworkExtProcFilterIntegrationTest, GrpcStreamFailureWithFailureModeAllow) {
  initializeWithFailureModeAllow();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc_filter"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(tcp_client->write("client_data", false));
  ProcessingRequest request;
  waitForFirstGrpcMessage(request);

  // Close gRPC stream without sending a response - with failure_mode_allow=true
  // the connection should not be closed
  closeGrpcStream();
  // TODO(botengyao) wait for the counter stats
  tcp_client->close();
}

// Test with read processing modes set to SKIP
TEST_P(NetworkExtProcFilterIntegrationTest, ProcessingModesReadSkip) {
  initializeWithSkipProcessingModes(true);
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc_filter"));

  // Write data from client to server - with process_read=SKIP, this should pass through
  // directly without invoking the external processor
  ASSERT_TRUE(tcp_client->write("client_data", false));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Data should be passed through without modification
  ASSERT_TRUE(fake_upstream_connection->waitForData(11));

  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->close();
}

// Test with write processing modes set to SKIP
TEST_P(NetworkExtProcFilterIntegrationTest, ProcessingModesWriteSkip) {
  initializeWithSkipProcessingModes(false);
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc_filter"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Data should be passed through without modification
  ASSERT_TRUE(fake_upstream_connection->write("server_response", false));
  tcp_client->waitForData("server_response");

  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->close();
}

// Test handling of multiple data chunks in a single direction
TEST_P(NetworkExtProcFilterIntegrationTest, MultipleDataChunks) {
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc_filter"));
  ASSERT_TRUE(tcp_client->write("chunk1", false));

  // Wait for the upstream connection
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // First chunk should be sent to ext_proc
  ProcessingRequest request1;
  waitForFirstGrpcMessage(request1);
  EXPECT_EQ(request1.has_read_data(), true);
  EXPECT_EQ(request1.read_data().data(), "chunk1");
  EXPECT_EQ(request1.read_data().end_of_stream(), false);

  sendReadGrpcMessage("chunk1_processed", false, true);
  ASSERT_TRUE(fake_upstream_connection->waitForData(16));

  ASSERT_TRUE(tcp_client->write("chunk2", true));

  // Second chunk should also be sent to ext_proc
  ProcessingRequest request2;
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request2));
  EXPECT_EQ(request2.has_read_data(), true);
  EXPECT_EQ(request2.read_data().data(), "chunk2");
  EXPECT_EQ(request2.read_data().end_of_stream(), true);

  // Respond to second chunk
  sendReadGrpcMessage("chunk2_processed", true);
  ASSERT_TRUE(fake_upstream_connection->waitForData(16));

  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->close();
}

// Test empty data handling
TEST_P(NetworkExtProcFilterIntegrationTest, EmptyDataHandling) {
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc_filter"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Send empty data with end_stream=true to indicate half-close
  ASSERT_TRUE(tcp_client->write("", true));

  // Empty data should still trigger ext_proc
  ProcessingRequest request;
  waitForFirstGrpcMessage(request);
  EXPECT_EQ(request.has_read_data(), true);
  EXPECT_EQ(request.read_data().data(), "");
  EXPECT_EQ(request.read_data().end_of_stream(), true);

  // Send back an empty response
  sendReadGrpcMessage("", true, true);

  // Server should see half-close
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());

  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->close();
}

} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
