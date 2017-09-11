#include <string>

#include "common/http/header_map_impl.h"
#include "common/protobuf/utility.h"

#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class ProtoIntegrationTest : public HttpIntegrationTest,
                             public testing::TestWithParam<Network::Address::IpVersion> {
public:
  ProtoIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void SetUp() override {}

  void initialize() {
    // Fake upstream.
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP1, version_));

    // Below we build up the config completely dynamically.
    // TODO(alyssawilk): Make some of this a static config as appropriate, i.e. the bits that won't
    // change.

    // Admin access log path and address.
    auto* admin = bootstrap_.mutable_admin();
    admin->set_access_log_path("/dev/null");
    auto* admin_socket_addr = admin->mutable_address()->mutable_socket_address();
    admin_socket_addr->set_address(Network::Test::getLoopbackAddressString(GetParam()));
    admin_socket_addr->set_port_value(0);

    // HTTP/1.1 Listener with HTTP connection manager filter.
    auto* static_resources = bootstrap_.mutable_static_resources();
    auto* listener = static_resources->mutable_listeners()->Add();
    listener->set_name("listener_0");
    auto* listener_socket_addr = listener->mutable_address()->mutable_socket_address();
    listener_socket_addr->set_address(Network::Test::getLoopbackAddressString(GetParam()));
    listener_socket_addr->set_port_value(0);
    auto* hcm_filter = listener->mutable_filter_chains()->Add()->mutable_filters()->Add();
    hcm_filter->set_name("http_connection_manager");
    envoy::api::v2::filter::HttpConnectionManager hcm_config;
    hcm_config.set_codec_type(envoy::api::v2::filter::HttpConnectionManager::HTTP1);
    auto* router_filter = hcm_config.mutable_http_filters()->Add();
    router_filter->set_name("router");
    (*router_filter->mutable_config()->mutable_fields())["deprecated_v1"].set_bool_value(true);
    // Route configuration.
    auto* route_config = hcm_config.mutable_route_config();
    route_config->set_name("route_config_0");
    auto* virtual_host = route_config->mutable_virtual_hosts()->Add();
    virtual_host->set_name("integration");
    virtual_host->add_domains("*");
    auto* route = virtual_host->mutable_routes()->Add();
    route->mutable_match()->set_prefix("/");
    route->mutable_route()->set_cluster("cluster_0");
    MessageUtil::jsonConvert(hcm_config, *hcm_filter->mutable_config());

    // Cluster for fake upstream.
    auto* cluster = static_resources->mutable_clusters()->Add();
    cluster->set_name("cluster_0");
    cluster->mutable_connect_timeout()->set_seconds(5);
    cluster->set_type(envoy::api::v2::Cluster::STATIC);
    cluster->set_lb_policy(envoy::api::v2::Cluster::ROUND_ROBIN);
    auto* host_socket_addr = cluster->mutable_hosts()->Add()->mutable_socket_address();
    host_socket_addr->set_address(Network::Test::getLoopbackAddressString(GetParam()));
    host_socket_addr->set_port_value(fake_upstreams_.back()->localAddress()->ip()->port());

    // Generate bootstrap JSON.
    const std::string bootstrap_path = TestEnvironment::writeStringToFileForTest(
        "bootstrap.json", MessageUtil::getJsonStringFromMessage(bootstrap_));
    createGeneratedApiTestServer(bootstrap_path, {"http"});
  }

  void set_source_address(std::string address_string) {
    bootstrap_.mutable_cluster_manager()
        ->mutable_upstream_bind_config()
        ->mutable_source_address()
        ->set_address(address_string);
  }

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  envoy::api::v2::Bootstrap bootstrap_;
};

TEST_P(ProtoIntegrationTest, TestBind) {
  std::string address_string;
  if (GetParam() == Network::Address::IpVersion::v4) {
    address_string = TestUtility::getIpv4Loopback();
  } else {
    address_string = "::1";
  }
  set_source_address(address_string);
  initialize();

  executeActions(
      {[&]() -> void { codec_client_ = makeHttpConnection(lookupPort("http")); },
       // Request 1.
       [&]() -> void {
         codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                    {":path", "/test/long/url"},
                                                                    {":scheme", "http"},
                                                                    {":authority", "host"}},
                                            1024, *response_);
       },
       [&]() -> void {
         fake_upstream_connection_ = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
         std::string address =
             fake_upstream_connection_->connection().remoteAddress().ip()->addressAsString();
         EXPECT_EQ(address, address_string);
       },
       [&]() -> void { upstream_request_ = fake_upstream_connection_->waitForNewStream(); },
       [&]() -> void { upstream_request_->waitForEndStream(*dispatcher_); },
       // Cleanup both downstream and upstream
       [&]() -> void { codec_client_->close(); },
       [&]() -> void { fake_upstream_connection_->close(); },
       [&]() -> void { fake_upstream_connection_->waitForDisconnect(); }});
}

TEST_P(ProtoIntegrationTest, TestFailedBind) {
  set_source_address("8.8.8.8");
  initialize();

  executeActions({[&]() -> void {
                    // Envoy will create and close some number of connections when trying to bind.
                    // Make sure they don't cause assertion failures when we ignore them.
                    fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
                  },
                  [&]() -> void { codec_client_ = makeHttpConnection(lookupPort("http")); },
                  [&]() -> void {
                    // With no ability to successfully bind on an upstream connection Envoy should
                    // send a 500.
                    codec_client_->makeHeaderOnlyRequest(
                        Http::TestHeaderMapImpl{{":method", "GET"},
                                                {":path", "/test/long/url"},
                                                {":scheme", "http"},
                                                {":authority", "host"},
                                                {"x-forwarded-for", "10.0.0.1"},
                                                {"x-envoy-upstream-rq-timeout-ms", "1000"}},
                        *response_);
                    response_->waitForEndStream();
                  },
                  [&]() -> void { cleanupUpstreamAndDownstream(); }});
  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("503", response_->headers().Status()->value().c_str());
  EXPECT_LT(0, test_server_->counter("cluster.cluster_0.bind_errors")->value());
}

INSTANTIATE_TEST_CASE_P(IpVersions, ProtoIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));
} // namespace
} // namespace Envoy
