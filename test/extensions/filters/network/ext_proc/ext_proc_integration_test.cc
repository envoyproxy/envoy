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

  void initialize() override {
    config_helper_.renameListener("network_ext_proc_filter");
    config_helper_.addConfigModifier(
        [&](::envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
          cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
          cluster->set_name("cluster_1");
          cluster->mutable_load_assignment()->set_cluster_name("cluster_1");
          Envoy::ConfigHelper::setHttp2WithMaxConcurrentStreams(
              *(bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(1)), 2);
        });
    BaseIntegrationTest::initialize();
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
    ENVOY_LOG_MISC(debug, "boteng sendReadGrpcMessage {}", response.DebugString());
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
    ENVOY_LOG_MISC(debug, "boteng sendReadGrpcMessage {}", response.DebugString());
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
    ENVOY_LOG_MISC(debug, "boteng sendWriteGrpcMessage {}", response.DebugString());
    if (first_message) {
      processor_stream_->startGrpcStream();
    }
    processor_stream_->sendGrpcMessage(response);
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
  sendReadGrpcMessage("client_data_inspected", true, true);

  ASSERT_TRUE(fake_upstream_connection->waitForData(21));

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
  tcp_client_3->close();
}

// Test that data is passed through both directions
TEST_P(NetworkExtProcFilterIntegrationTest, TcpProxyBidirectionalUpstreamHalfClose) {
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
  sendReadGrpcMessage("client_data_inspected", true, true);

  ASSERT_TRUE(fake_upstream_connection->waitForData(21));

  // Write data from server to client
  ASSERT_TRUE(fake_upstream_connection->write("server_response", true));

  ProcessingRequest write_request;
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, write_request));
  sendWriteGrpcMessage("server_data_inspected", true);

  // Verify client received the data
  tcp_client->waitForData("server_data_inspected");

  // Close everything
  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(processor_stream_->waitForEndStream(*dispatcher_));
  tcp_client->close();
}

TEST_P(NetworkExtProcFilterIntegrationTest, TcpProxyDownstreamHalfCloseBothWays) {
  initialize();

  Envoy::IntegrationTcpClientPtr tcp_client =
      makeTcpConnection(lookupPort("network_ext_proc_filter"));

  Envoy::FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(fake_upstream_connection->write("server_data", true));

  ProcessingRequest request;
  waitForFirstGrpcMessage(request);
  sendWriteGrpcMessage("server_data_inspected", true, true);

  tcp_client->waitForData("server_data_inspected");

  // Use true here, and listener connection will get remote close.
  // and the disableClose(true) will take effect to delay the deletion of the filter chain.
  ASSERT_TRUE(tcp_client->write("client_data", true));
  ProcessingRequest write_request;
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, write_request));
  sendReadGrpcMessage("client_data_inspected", true);

  ASSERT_TRUE(fake_upstream_connection->waitForData(21));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  ASSERT_TRUE(processor_stream_->waitForEndStream(*dispatcher_));
  tcp_client->close();
}

} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
