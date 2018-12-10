#include "envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/discovery.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/stats/scope.h"

#include "common/config/protobuf_link_hacks.h"
#include "common/config/resources.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/utility.h"
#include "test/integration/xds_integration_test_base.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::AssertionFailure;
using testing::AssertionResult;
using testing::AssertionSuccess;
using testing::IsSubstring;

namespace Envoy {
namespace {

const std::string kConfig = R"EOF(
admin:
  access_log_path: /dev/null
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
)EOF";

class CdsIntegrationTest : public XdsIntegrationTestBase,
                           public Grpc::GrpcClientIntegrationParamTest {
public:
  CdsIntegrationTest()
      : XdsIntegrationTestBase(Http::CodecClient::Type::HTTP2, ipVersion(), kConfig) {}

  void TearDown() override {
    AssertionResult result = xds_connection_->close();
    RELEASE_ASSERT(result, result.message());
    result = xds_connection_->waitForDisconnect();
    RELEASE_ASSERT(result, result.message());
    xds_connection_.reset();
    test_server_.reset();
    fake_upstreams_.clear();
  }

  envoy::api::v2::Cluster buildCluster(const std::string& name) {
    return TestUtility::parseYaml<envoy::api::v2::Cluster>(
        fmt::format(R"EOF(
      name: {}
      connect_timeout: 5s
      type: STATIC
      load_assignment:
        cluster_name: {}
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: {}
                  port_value: {}
      lb_policy: ROUND_ROBIN
      http2_protocol_options: {{}}
    )EOF",
                    name, name, Network::Test::getLoopbackAddressString(ipVersion()),
                    fake_upstreams_[0]->localAddress()->ip()->port()));
  }

  void initializeCds() {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      // Setup cds and corresponding gRPC cluster.
      auto* cds_config =
          bootstrap.mutable_dynamic_resources()->mutable_cds_config()->mutable_api_config_source();
      bootstrap.mutable_dynamic_resources()->clear_ads_config();
      bootstrap.mutable_dynamic_resources()->clear_lds_config();

      cds_config->set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);
      cds_config->mutable_request_timeout()->set_seconds(1);
      auto* grpc_service = cds_config->add_grpc_services();
      setGrpcService(*grpc_service, "my_cds_cluster", fake_upstreams_[0]->localAddress());
      grpc_service->mutable_envoy_grpc()->set_cluster_name("my_cds_cluster");

      auto* cds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      cds_cluster->set_name("my_cds_cluster");
      cds_cluster->mutable_connect_timeout()->set_seconds(5);
      auto* sockaddr = cds_cluster->add_hosts()->mutable_socket_address();
      sockaddr->set_protocol(envoy::api::v2::core::SocketAddress::TCP);
      sockaddr->set_address(Network::Test::getLoopbackAddressString(ipVersion()));
      sockaddr->set_port_value(0);
      cds_cluster->clear_http2_protocol_options();
    });
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);

    HttpIntegrationTest::initialize();

    fake_upstreams_[0]->set_allow_unexpected_disconnects(false);
    // Causes xds_connection_ to be filled with a newly constructed FakeHttpConnection.
    AssertionResult result =
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();

    EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}));
    sendDiscoveryResponse<envoy::api::v2::Cluster>(Config::TypeUrl::get().Cluster,
                                                   {buildCluster("cluster_0")}, "1");
  }
};

// GoogleGrpc causes problems.
INSTANTIATE_TEST_CASE_P(IpVersionsClientType, CdsIntegrationTest,
                        testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                                         testing::Values(Grpc::ClientType::EnvoyGrpc)));

// Tests Envoy HTTP health checking a single healthy endpoint and reporting that it is
// indeed healthy to the server.
TEST_P(CdsIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  // Controls how many fake_upstreams_.emplace_back(new FakeUpstream) will happen in
  // BaseIntegrationTest::createUpstreams() (which is part of initialize()).
  setUpstreamCount(1);
  initializeCds();

  // Adapted from HttpIntegrationTest::testRouterRequestAndResponseWithBody(1024, 512, false).
  int request_size = 1024;
  int response_size = 512;
  codec_client_ =
      makeHttpConnection(makeClientConnection(fake_upstreams_[0]->localAddress()->ip()->port()));
  Http::TestHeaderMapImpl request_headers{
      {":method", "POST"},    {":path", "/test/long/url"}, {":scheme", "http"},
      {":authority", "host"}, {"x-lyft-user-id", "123"},   {"x-forwarded-for", "10.0.0.1"}};
  auto response = sendRequestAndWaitForResponse(request_headers, request_size,
                                                default_response_headers_, response_size);
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(request_size, upstream_request_->bodyLength());

  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(response_size, response->body().size());

  cleanupUpstreamAndDownstream();
  fake_upstream_connection_ = nullptr;
}

} // namespace
} // namespace Envoy
