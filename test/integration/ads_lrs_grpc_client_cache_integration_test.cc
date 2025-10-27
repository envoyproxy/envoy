#include <chrono>
#include <memory>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/test_runtime.h"

namespace Envoy {
namespace {

// Test class to test the behavior of shared v/s unique gRPC clients for ADS and
// LRS.
class AdsLrsGrpcClientCacheIntegrationTest
    : public Grpc::BaseGrpcClientIntegrationParamTest,
      public HttpIntegrationTest,
      public testing::TestWithParam<
          std::tuple<Network::Address::IpVersion, Grpc::ClientType, std::string>> {
public:
  AdsLrsGrpcClientCacheIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion()) {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
    setCachedGrpcCLientForXdsFeatureValue();
  }

  Network::Address::IpVersion ipVersion() const override { return std::get<0>(GetParam()); }
  Grpc::ClientType clientType() const override { return std::get<1>(GetParam()); }
  std::string getCachedGrpcCLientForXdsFeatureValue() { return std::get<2>(GetParam()); }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Define the common gRPC cluster for both ADS and LRS
      auto* xds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      xds_cluster->set_name("xds_cluster");
      xds_cluster->set_type(envoy::config::cluster::v3::Cluster::STATIC);
      xds_cluster->mutable_connect_timeout()->set_seconds(5);
      ConfigHelper::setHttp2(*xds_cluster); // Ensure HTTP/2 is used

      auto* load_assignment = xds_cluster->mutable_load_assignment();
      load_assignment->set_cluster_name("xds_cluster");
      auto* locality_lb_endpoints = load_assignment->add_endpoints();
      auto* lb_endpoint = locality_lb_endpoints->add_lb_endpoints();
      auto* endpoint = lb_endpoint->mutable_endpoint();
      auto* address = endpoint->mutable_address()->mutable_socket_address();
      address->set_address(Network::Test::getLoopbackAddressString(ipVersion()));
      address->set_port_value(xds_upstream_->localAddress()->ip()->port());

      lb_endpoint = locality_lb_endpoints->add_lb_endpoints();
      endpoint = lb_endpoint->mutable_endpoint();
      address = endpoint->mutable_address()->mutable_socket_address();
      address->set_address(Network::Test::getLoopbackAddressString(ipVersion()));
      address->set_port_value(xds_upstream2_->localAddress()->ip()->port());

      // Configure ADS to use the xds_cluster
      auto* ads_api_config = bootstrap.mutable_dynamic_resources()->mutable_ads_config();
      ads_api_config->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
      ads_api_config->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
      ads_api_config->clear_grpc_services(); // Clear any defaults
      auto* ads_grpc_service = ads_api_config->add_grpc_services();
      setGrpcService(*ads_grpc_service, "xds_cluster", xds_upstream_->localAddress());

      // Configure LRS to use the same xds_cluster
      auto* load_stats_config = bootstrap.mutable_cluster_manager()->mutable_load_stats_config();
      load_stats_config->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
      load_stats_config->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
      load_stats_config->clear_grpc_services(); // Clear any defaults
      auto* lrs_grpc_service = load_stats_config->add_grpc_services();
      setGrpcService(*lrs_grpc_service, "xds_cluster", xds_upstream_->localAddress());

      // Add a dummy static cluster to trigger LRS
      auto* data_cluster = bootstrap.mutable_static_resources()->add_clusters();
      data_cluster->set_name("data_cluster");
      data_cluster->set_type(envoy::config::cluster::v3::Cluster::STATIC);
      data_cluster->mutable_connect_timeout()->set_seconds(5);
      data_cluster->mutable_load_assignment()->set_cluster_name("data_cluster");
    });

    config_helper_.skipPortUsageValidation();
    HttpIntegrationTest::initialize();
  }

  void setCachedGrpcCLientForXdsFeatureValue() {
    scoped_runtime_.mergeValues({{"envoy.restart_features.use_cached_grpc_client_for_xds",
                                  getCachedGrpcCLientForXdsFeatureValue()}});
  }

  void createUpstreams() override {
    xds_upstream_ = &addFakeUpstream(FakeHttpConnection::Type::HTTP2);
    xds_upstream2_ = &addFakeUpstream(FakeHttpConnection::Type::HTTP2);

    // Create backends and initialize their wrapper.
    HttpIntegrationTest::createUpstreams();
  }

  static std::string
  testParamsToString(const ::testing::TestParamInfo<
                     std::tuple<Network::Address::IpVersion, Grpc::ClientType, std::string>>& p) {
    return fmt::format(
        "{}_{}_{}", TestUtility::ipVersionToString(std::get<0>(p.param)),
        std::get<1>(p.param) == Grpc::ClientType::GoogleGrpc ? "GoogleGrpc" : "EnvoyGrpc",
        std::get<2>(p.param).compare("true") == 0 ? "SharedGrpcClients" : "UniqueGrpcClients");
  }

protected:
  FakeUpstream* xds_upstream_;
  FakeUpstream* xds_upstream2_;

private:
  TestScopedRuntime scoped_runtime_;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersions, AdsLrsGrpcClientCacheIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),
                     testing::Values("true", "false")),
    AdsLrsGrpcClientCacheIntegrationTest::testParamsToString);

// Verify that using shared clients does not result in any crashes in an
// integration test.
TEST_P(AdsLrsGrpcClientCacheIntegrationTest, Basic) {
  initialize();

  // Envoy will start and connect to the fake upstream for ADS.
  FakeHttpConnectionPtr ads_connection;
  ASSERT_TRUE(xds_upstream_->waitForHttpConnection(*dispatcher_, ads_connection));

  // We expect an ADS stream to be established.
  FakeStreamPtr ads_stream;
  ASSERT_TRUE(ads_connection->waitForNewStream(*dispatcher_, ads_stream));
  ads_stream->startGrpcStream();

  // To trigger LRS, some cluster stats need to be reported. This usually
  // happens if a cluster is actively used. Since we added "data_cluster", Envoy
  // should attempt to report stats for it.
  FakeHttpConnectionPtr lrs_connection;
  FakeStreamPtr lrs_stream;
  if (clientType() == Grpc::ClientType::EnvoyGrpc) {
    // EnvoyGrpc will pick a different host for the new stream, so we need a new
    // connection.
    EXPECT_TRUE(xds_upstream2_->waitForHttpConnection(*dispatcher_, lrs_connection,
                                                      std::chrono::milliseconds(500)));
    EXPECT_TRUE(lrs_connection->waitForNewStream(*dispatcher_, lrs_stream));
  } else {
    EXPECT_TRUE(ads_connection->waitForNewStream(*dispatcher_, lrs_stream));
  }
  lrs_stream->startGrpcStream();

  // Cleanup
  if (ads_connection) {
    ASSERT_TRUE(ads_connection->close());
    ASSERT_TRUE(ads_connection->waitForDisconnect());
  }
  if (lrs_connection) {
    ASSERT_TRUE(lrs_connection->close());
    ASSERT_TRUE(lrs_connection->waitForDisconnect());
  }
}

} // namespace
} // namespace Envoy
