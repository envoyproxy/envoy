#include <string>

#include "envoy/common/platform.h"
#include "envoy/config/cluster/v3/cluster.pb.h"

#include "integration_tcp_client.h"
#include "source/common/common/macros.h"
#include "source/common/tcp_proxy/tcp_proxy.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/ads_integration.h"
#include "test/integration/fake_upstream.h"
#include "test/integration/integration.h"
#include "test/test_common/resources.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class TcpProxyOdcdsIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                     public BaseIntegrationTest {
public:
  TcpProxyOdcdsIntegrationTest()
      : BaseIntegrationTest(std::get<0>(GetParam()), ConfigHelper::tcpProxyConfig()) {
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Set xds_cluster.
      auto* static_resources = bootstrap.mutable_static_resources();
      ASSERT(static_resources->clusters_size() == 1);
      auto& cluster_protocol_options =
          *static_resources->mutable_clusters(0)->mutable_typed_extension_protocol_options();
      envoy::extensions::upstreams::http::v3::HttpProtocolOptions h2_options;
      h2_options.mutable_explicit_http_config()->mutable_http2_protocol_options();
      cluster_protocol_options["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"].PackFrom(
          h2_options);

      // Add on demand config to tcp_proxy config.
      ASSERT(static_resources->listeners_size() == 1);
      auto* config_blob = static_resources->mutable_listeners(0)
                              ->mutable_filter_chains(0)
                              ->mutable_filters(0)
                              ->mutable_typed_config();

      ASSERT_TRUE(config_blob->Is<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>());
      auto tcp_proxy_config =
          MessageUtil::anyConvert<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>(
              *config_blob);
      tcp_proxy_config.set_cluster("new_cluster");
      tcp_proxy_config.mutable_on_demand()->CopyFrom(
          TestUtility::parseYaml<
              envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy_OnDemand>(R"EOF(
          odcds_config:
            resource_api_version: V3
            api_config_source:
              api_type: DELTA_GRPC
              transport_api_version: V3
              grpc_services:
                envoy_grpc:
                  cluster_name: cluster_0
      )EOF"));
      config_blob->PackFrom(tcp_proxy_config);
    });

    // The test envoy use static listener and cluster for xDS.
    // The test framework does not update the above static resources.
    // Another upstream will be serving delta CDS requests and the response is explicitly
    // generated by test case rather than the integration test framework.
    use_lds_ = false;
    enableHalfClose(true);
  }
  void TearDown() override { cleanUpXdsConnection(); }

  void initialize() override {
    // The first upstream serves the ADS request including the future on demand CDS request.
    setUpstreamCount(1);
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);

    BaseIntegrationTest::initialize();

    // HTTP protocol version is not used because tcp stream is expected.
    addFakeUpstream(FakeHttpConnection::Type::HTTP2);

    new_cluster_ = ConfigHelper::buildStaticCluster(
        "new_cluster", fake_upstreams_.back()->localAddress()->ip()->port(),
        Network::Test::getLoopbackAddressString(ipVersion()));

    test_server_->waitUntilListenersReady();
    registerTestServerPorts({"tcp_proxy"});
  }

  // The on demand CDS stream. The requested cluster config is delivered to Envoy in this stream.
  FakeStreamPtr odcds_stream_;
  // The prepared cluster config response.
  envoy::config::cluster::v3::Cluster new_cluster_;
  uint32_t expected_upstream_connections_{1};
  std::vector<FakeRawConnectionPtr> fake_upstream_connections_;
  std::vector<IntegrationTcpClientPtr> tcp_clients_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, TcpProxyOdcdsIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(TcpProxyOdcdsIntegrationTest, SingleTcpClient) {
  initialize();

  // Establish a tcp request to the Envoy.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // The delta CDS stream is established.
  auto result = fake_upstreams_.front()->waitForHttpConnection(*dispatcher_, xds_connection_);
  RELEASE_ASSERT(result, result.message());
  result = xds_connection_->waitForNewStream(*dispatcher_, odcds_stream_);
  RELEASE_ASSERT(result, result.message());
  odcds_stream_->startGrpcStream();

  // Verify the delta CDS request and respond with the prepared ``new_cluster``.
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().Cluster, {"new_cluster"}, {},
                                           odcds_stream_));
  sendDeltaDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().Cluster, {new_cluster_}, {}, "1", odcds_stream_);
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().Cluster, {}, {}, odcds_stream_));

  // This upstream is listening on the endpoint of ``new_cluster``. It starts to serve tcp_proxy.
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_.back()->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(fake_upstream_connection->write("hello"));
  tcp_client->waitForData("hello");

  ASSERT_TRUE(tcp_client->write("world"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

// Verify only one delta xds response is needed for multiple tcp_proxy requests.
TEST_P(TcpProxyOdcdsIntegrationTest, RepeatedRequest) {
  expected_upstream_connections_ = 10;
  initialize();

  // Establish tcp connections to the target Envoy.
  for (auto n = expected_upstream_connections_; n != 0; n--) {
    tcp_clients_.push_back(makeTcpConnection(lookupPort("tcp_proxy")));
  }

  // The delta CDS stream is established.
  auto result = fake_upstreams_.front()->waitForHttpConnection(*dispatcher_, xds_connection_);
  RELEASE_ASSERT(result, result.message());
  result = xds_connection_->waitForNewStream(*dispatcher_, odcds_stream_);
  RELEASE_ASSERT(result, result.message());
  odcds_stream_->startGrpcStream();

  // Verify the delta CDS request and respond without providing the cluster.
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().Cluster, {"new_cluster"}, {},
                                           odcds_stream_));
  sendDeltaDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().Cluster, {new_cluster_}, {}, "1", odcds_stream_);
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().Cluster, {}, {}, odcds_stream_));

  // This upstream is listening on the endpoint of ``new_cluster``.
  for (auto n = expected_upstream_connections_; n != 0; n--) {
    fake_upstream_connections_.push_back(nullptr);
    auto& fake_upstream_connection = fake_upstream_connections_.back();
    ASSERT_TRUE(fake_upstreams_.back()->waitForRawConnection(fake_upstream_connection));
    ASSERT_TRUE(fake_upstream_connection->write("hello"));
  }

  for (auto& tcp_client : tcp_clients_) {
    tcp_client->waitForData("hello");
    ASSERT_TRUE(tcp_client->write("world"));
  }

  for (auto& fake_upstream_connection : fake_upstream_connections_) {
    ASSERT_TRUE(fake_upstream_connection->waitForData(5));
    ASSERT_TRUE(fake_upstream_connection->close());
    ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  }

  for (auto& tcp_client : tcp_clients_) {
    tcp_client->waitForHalfClose();
    tcp_client->close();
  }
}

TEST_P(TcpProxyOdcdsIntegrationTest, ShutdownConnectionOnTimeout) {
  initialize();

  // Establish a tcp request to the Envoy.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // The delta CDS stream is established.
  auto result = fake_upstreams_.front()->waitForHttpConnection(*dispatcher_, xds_connection_);
  RELEASE_ASSERT(result, result.message());
  result = xds_connection_->waitForNewStream(*dispatcher_, odcds_stream_);
  RELEASE_ASSERT(result, result.message());
  odcds_stream_->startGrpcStream();

  // Verify the delta CDS request and respond without providing the cluster.
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().Cluster, {"new_cluster"}, {},
                                           odcds_stream_));
  sendDeltaDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                                  {}, {}, "1", odcds_stream_);
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().Cluster, {}, {}, odcds_stream_));

  tcp_client->waitForHalfClose();
  tcp_client->close();
}

TEST_P(TcpProxyOdcdsIntegrationTest, ShutdownConnectionOnClusterMissing) {
  initialize();

  // Establish a tcp request to the Envoy.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // The delta CDS stream is established.
  auto result = fake_upstreams_.front()->waitForHttpConnection(*dispatcher_, xds_connection_);
  RELEASE_ASSERT(result, result.message());
  result = xds_connection_->waitForNewStream(*dispatcher_, odcds_stream_);
  RELEASE_ASSERT(result, result.message());
  odcds_stream_->startGrpcStream();

  // Verify the delta CDS request and respond the required cluster is missing.
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().Cluster, {"new_cluster"}, {},
                                           odcds_stream_));
  sendDeltaDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
      Config::TypeUrl::get().Cluster, {}, {"new_cluster"}, "1", odcds_stream_);
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().Cluster, {}, {}, odcds_stream_));

  tcp_client->waitForHalfClose();
  tcp_client->close();
}

TEST_P(TcpProxyOdcdsIntegrationTest, ShutdownTcpClientBeforeOdcdsResponse) {
  initialize();

  // Establish a tcp request to the Envoy.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // The delta CDS stream is established.
  auto result = fake_upstreams_.front()->waitForHttpConnection(*dispatcher_, xds_connection_);
  RELEASE_ASSERT(result, result.message());
  result = xds_connection_->waitForNewStream(*dispatcher_, odcds_stream_);
  RELEASE_ASSERT(result, result.message());
  odcds_stream_->startGrpcStream();

  // Verify the delta CDS request and respond the required cluster is missing.
  EXPECT_TRUE(compareDeltaDiscoveryRequest(Config::TypeUrl::get().Cluster, {"new_cluster"}, {},
                                           odcds_stream_));
  tcp_client->close();
}

} // namespace
} // namespace Envoy
