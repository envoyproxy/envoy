#include <string>

#include "source/extensions/filters/network/ext_proc/config.h"
#include "envoy/service/network_ext_proc/v3/network_external_processor.pb.h"

// #include "src/envoy/http/codec.h"
#include "test/config/utility.h"
#include "test/integration/base_integration_test.h"
#include "test/integration/fake_upstream.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtProc {

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
  }

  void createUpstreams() override {
    BaseIntegrationTest::createUpstreams();
    grpc_upstream_ = &addFakeUpstream(Envoy::HTTP::CodecType::HTTP2);
  } 

  void initialize() override {
    config_helper_.renameListener("network_ext_proc_filter");
    config_helper_.addConfigModifier(
        [&](::envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
          cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
          cluster->set_name("cluster_1");
          cluster->mutable_load_assignment()->set_cluster_name("cluster_1");
          Envoy::ConfigHelper::setHttp2(*(
              bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(
                  1)));
        });
    BaseIntegrationTest::initialize();
  }

  void waitForFirstGrpcMessage(ProcessingRequest& request) {
    ENVOY_LOG_MISC(debug, "waitForFirstGrpcMessage {}",
                   grpc_upstream_->localAddress()->asString());
    ASSERT_TRUE(grpc_upstream_->waitForHttpConnection(*dispatcher_,
                                                      processor_connection_));
    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_,
                                                        processor_stream_));
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
  }

  void sendReadGrpcMessage(std::string data, bool end_stream,
                           bool first_message = false) {
    ProcessingResponse response;
    auto* read_data = response.mutable_read_data();
    read_data->set_data(data);
    read_data->set_end_stream(end_stream);
    ENVOY_LOG_MISC(debug, "boteng sendReadGrpcMessage {}",
                   response.DebugString());
    if (first_message) {
      processor_stream_->startGrpcStream();
    }
    processor_stream_->sendGrpcMessage(response);
  }

  void sendReadGrpcMessageWithStream(Envoy::FakeStreamPtr& stream,
                                     std::string data, bool end_stream,
                                     bool first_message = false) {
    ProcessingResponse response;
    auto* read_data = response.mutable_read_data();
    read_data->set_data(data);
    read_data->set_end_stream(end_stream);
    ENVOY_LOG_MISC(debug, "boteng sendReadGrpcMessage {}",
                   response.DebugString());
    if (first_message) {
      stream->startGrpcStream();
    }
    stream->sendGrpcMessage(response);
  }

  void sendWriteGrpcMessage(std::string data, bool end_stream,
                            bool first_message = false) {
    ProcessingResponse response;
    auto* write_data = response.mutable_write_data();
    write_data->set_data(data);
    write_data->set_end_stream(end_stream);
    ENVOY_LOG_MISC(debug, "boteng sendWriteGrpcMessage {}",
                   response.DebugString());
    if (first_message) {
      processor_stream_->startGrpcStream();
    }
    processor_stream_->sendGrpcMessage(response);
  }

  // void waitForShutdown() {
  //   // Create a timer that exits the dispatcher after 5 seconds
  //   Envoy::Event::TimerPtr shutdown_timer = dispatcher_->createTimer([this]() {
  //     dispatcher_->exit();  // Exit the event loop
  //   });

  //   // Enable the timer
  //   shutdown_timer->enableTimer(std::chrono::milliseconds(4000));

  //   // Run the dispatcher until the shutdown timer fires
  //   dispatcher_->run(Envoy::Event::Dispatcher::RunType::RunUntilExit);
  // }

  Envoy::FakeUpstream* grpc_upstream_;
  Envoy::FakeHttpConnectionPtr processor_connection_;
  Envoy::FakeStreamPtr processor_stream_;
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
  ASSERT_TRUE(
      fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(tcp_client->write("client_data", true));

  ProcessingRequest request;
  waitForFirstGrpcMessage(request);
  sendReadGrpcMessage("client_data_inspected", true, true);

  ASSERT_TRUE(fake_upstream_connection->waitForData(21));

  tcp_client->close();
}

// // Test that data is passed through both directions
// TEST_P(NetworkExtProcFilterIntegrationTest, TcpProxyBidirectional) {
//   initialize();

//   // Connect to the server
//   IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc"));

//   // Write data from client to server
//   ASSERT_TRUE(tcp_client->write("client_data", false));

//   // Wait for the upstream connection and verify data
//   FakeRawConnectionPtr fake_upstream_connection;
//   ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
//   ASSERT_TRUE(fake_upstream_connection->waitForData(11));

//   // Write data from server to client
//   ASSERT_TRUE(fake_upstream_connection->write("server_response", false));

//   // Verify client received the data
//   tcp_client->waitForData("server_response");

//   // Close everything
//   ASSERT_TRUE(fake_upstream_connection->close());
//   tcp_client->close();
// }

} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
