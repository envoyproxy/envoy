#include "test/integration/integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class TcpAsyncClientIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                      public BaseIntegrationTest {
public:
  TcpAsyncClientIntegrationTest()
      : BaseIntegrationTest(GetParam(), absl::StrCat(ConfigHelper::baseConfig(), R"EOF(
    filter_chains:
    - filters:
      - name: envoy.test.test_network_async_tcp_filter
        typed_config:
          "@type": type.googleapis.com/test.integration.filters.TestNetworkAsyncTcpFilterConfig
          cluster_name: cluster_0
    )EOF")) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersions, TcpAsyncClientIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(TcpAsyncClientIntegrationTest, SingleRequest) {
  enableHalfClose(true);
  initialize();

  std::string request("request");
  std::string response("response");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  test_server_->waitForCounterEq("test_network_async_tcp_filter.on_new_connection", 1);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_cx_active", 1);
  test_server_->waitForNumHistogramSamplesGe("cluster.cluster_0.upstream_cx_connect_ms", 1);
  ASSERT_TRUE(tcp_client->write(request, true));
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_tx_bytes_total", request.size());
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(request.size()));
  ASSERT_TRUE(fake_upstream_connection->write(response, true));
  test_server_->waitForCounterGe("test_network_async_tcp_filter.on_receive_async_data", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_rx_bytes_total", response.size());
  ASSERT_TRUE(tcp_client->waitForData(response.size()));
  tcp_client->close();
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy_local", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_total", 1);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_cx_active", 0);
  test_server_->waitForNumHistogramSamplesGe("cluster.cluster_0.upstream_cx_length_ms", 1);
}

TEST_P(TcpAsyncClientIntegrationTest, MultipleRequestFrames) {
  enableHalfClose(true);
  initialize();

  std::string data_frame_1("data_frame_1");
  std::string data_frame_2("data_frame_2");
  std::string data_frame_3("data_frame_3");
  std::string response_1("response_1");
  std::string response_2("response_2");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));

  // send data frame 1, 2, 3
  ASSERT_TRUE(tcp_client->write(data_frame_1, false));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(tcp_client->write(data_frame_2, false));
  ASSERT_TRUE(tcp_client->write(data_frame_3, true));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(3 * data_frame_1.size(), &data));
  ASSERT_TRUE(data == data_frame_1 + data_frame_2 + data_frame_3);

  // The following 2 write file events could be merged to one actual write with
  // the buffered data in the socket. We can continue sending data until the
  // client receives the first data frame. Sending them in a tight sequence also
  // works, but the onData calling times could be changed due to the event loop.
  ASSERT_TRUE(fake_upstream_connection->write(response_1, false));
  test_server_->waitForCounterGe("test_network_async_tcp_filter.on_receive_async_data", 1);
  ASSERT_TRUE(fake_upstream_connection->write(response_2, true));
  test_server_->waitForCounterGe("test_network_async_tcp_filter.on_receive_async_data", 2);
  tcp_client->waitForData(response_1 + response_2, true);
  tcp_client->close();
}

TEST_P(TcpAsyncClientIntegrationTest, MultipleResponseFrames) {
  enableHalfClose(true);
  initialize();

  std::string data_frame_1("data_frame_1");
  std::string response_1("response_1");
  std::string response_2("response_2");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));

  // send request 1
  ASSERT_TRUE(tcp_client->write(data_frame_1, true));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(data_frame_1.size(), &data));
  EXPECT_EQ(data_frame_1, data);

  // get response 1
  ASSERT_TRUE(fake_upstream_connection->write(response_1, false));
  test_server_->waitForCounterGe("test_network_async_tcp_filter.on_receive_async_data", 1);
  ASSERT_TRUE(fake_upstream_connection->write(response_2, true));
  test_server_->waitForCounterGe("test_network_async_tcp_filter.on_receive_async_data", 2);
  tcp_client->waitForData(response_1 + response_2, true);
  tcp_client->close();
}

// Test if RST close can be detected from downstream and upstream is closed by RST.
TEST_P(TcpAsyncClientIntegrationTest, TestClientCloseRST) {
  enableHalfClose(true);
  initialize();

  std::string request("request");
  std::string response("response");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  test_server_->waitForCounterEq("test_network_async_tcp_filter.on_new_connection", 1);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_cx_active", 1);
  test_server_->waitForNumHistogramSamplesGe("cluster.cluster_0.upstream_cx_connect_ms", 1);
  ASSERT_TRUE(tcp_client->write(request, false));
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_tx_bytes_total", request.size());
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(request.size()));
  ASSERT_TRUE(fake_upstream_connection->write(response, false));
  test_server_->waitForCounterGe("test_network_async_tcp_filter.on_receive_async_data", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_rx_bytes_total", response.size());
  ASSERT_TRUE(tcp_client->waitForData(response.size()));

  tcp_client->close(Network::ConnectionCloseType::AbortReset);

  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy_local", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_total", 1);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_cx_active", 0);
  test_server_->waitForNumHistogramSamplesGe("cluster.cluster_0.upstream_cx_length_ms", 1);
  ASSERT_TRUE(fake_upstream_connection->waitForRstDisconnect());
}

// Test if RST close can be detected from upstream.
TEST_P(TcpAsyncClientIntegrationTest, TestUpstreamCloseRST) {
  enableHalfClose(true);
  initialize();

  std::string request("request");
  std::string response("response");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  test_server_->waitForCounterEq("test_network_async_tcp_filter.on_new_connection", 1);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_cx_active", 1);
  test_server_->waitForNumHistogramSamplesGe("cluster.cluster_0.upstream_cx_connect_ms", 1);
  ASSERT_TRUE(tcp_client->write(request, false));
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_tx_bytes_total", request.size());
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(request.size()));
  ASSERT_TRUE(fake_upstream_connection->write(response, false));
  test_server_->waitForCounterGe("test_network_async_tcp_filter.on_receive_async_data", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_rx_bytes_total", response.size());
  ASSERT_TRUE(tcp_client->waitForData(response.size()));

  ASSERT_TRUE(fake_upstream_connection->close(Network::ConnectionCloseType::AbortReset));

  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy_remote", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_total", 1);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_cx_active", 0);
  test_server_->waitForNumHistogramSamplesGe("cluster.cluster_0.upstream_cx_length_ms", 1);
  tcp_client->waitForDisconnect();
}

#if !defined(WIN32)
// Test the behaviour when the connection is half closed and then the connection is reset by
// the client. The behavior is different for windows, since RST support is literally supported for
// unix like system, disabled the test for windows.
TEST_P(TcpAsyncClientIntegrationTest, TestDownstremHalfClosedThenRST) {
  enableHalfClose(true);
  initialize();

  std::string request("request");
  std::string response("response");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));

  // It is half-closed for downstream.
  ASSERT_TRUE(tcp_client->write(request, true));
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_tx_bytes_total", request.size());
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(request.size()));

  // Then the downstream is closed by RST. Listener socket will not try to read the I/O
  // since it is half closed.
  tcp_client->close(Network::ConnectionCloseType::AbortReset);

  // When the server tries to write to downstream, we will get Broken Pipe error, which is
  // RemoteClose event from downstream rather than RemoteReset.
  ASSERT_TRUE(fake_upstream_connection->write(response, false));

  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy_local", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_total", 1);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_cx_active", 0);
  test_server_->waitForNumHistogramSamplesGe("cluster.cluster_0.upstream_cx_length_ms", 1);

  // As a basic half close process, the connection is already half closed in Envoy before.
  // The normal close in Envoy will not trigger the remote close event for the upstream connection.
  // This is the same behavior as the normal half close process without detection of RST.
  ASSERT_TRUE(fake_upstream_connection->write(" ", true));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}
#endif

} // namespace
} // namespace Envoy
