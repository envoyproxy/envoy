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
dynamic_resources:
  cds_config:
    api_config_source:
      api_type: GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: my_cds_cluster
static_resources:
  clusters:
  - name: cluster_0
    http2_protocol_options: {}
    hosts:
      socket_address:
        address: 127.0.0.1
        port_value: 0
  - name: my_cds_cluster
    http2_protocol_options: {}
    hosts:
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
    XdsIntegrationTestBase::initialize();

    fake_upstreams_[0]->set_allow_unexpected_disconnects(false);
    fake_upstreams_[1]->set_allow_unexpected_disconnects(false);
    // Causes xds_connection_ to be filled with a newly constructed FakeHttpConnection.
    AssertionResult result =
        fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, xds_connection_);
    RELEASE_ASSERT(result, result.message());
    result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();
  }
};

INSTANTIATE_TEST_CASE_P(IpVersionsClientType, CdsIntegrationTest, GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(CdsIntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  // Controls how many fake_upstreams_.emplace_back(new FakeUpstream) will happen in
  // BaseIntegrationTest::createUpstreams() (which is part of initialize()).
  // Make sure this number matches the size of the 'clusters' repeated field in the bootstrap
  // config that you use!
  setUpstreamCount(2);
  setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);

  initializeCds();

  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}));
  sendDiscoveryResponse<envoy::api::v2::Cluster>(Config::TypeUrl::get().Cluster,
                                                 {buildCluster("cluster_0")}, "1");
  std::cerr << "finished CDS req/resp!!!!!!" << std::endl;
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
