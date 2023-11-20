#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/upstream/local_address_selector/v3/default_local_address_selector.pb.h"
#include "envoy/upstream/upstream.h"

#include "test/common/upstream/test_local_address_selector.h"
#include "test/integration/http_protocol_integration.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Upstream {

// Basic test that instantiates the ``DefaultLocalAddressSelector``.
TEST_P(HttpProtocolIntegrationTest, Basic) {

  // Specify both IPv4 and IPv6 source addresses, so the right one is picked
  // based on IP version of upstream.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    uint32_t const port_value = 1234;
    auto bind_config = bootstrap.mutable_cluster_manager()->mutable_upstream_bind_config();
    bind_config->mutable_source_address()->set_address("127.0.0.1");
    bind_config->mutable_source_address()->set_port_value(port_value);
    auto extra_source_address = bind_config->add_extra_source_addresses();
    extra_source_address->mutable_address()->set_address("::1");
    extra_source_address->mutable_address()->set_port_value(port_value);
  });

  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  // Verify the proxied request was received upstream, as expected.
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  // Verify the proxied response was received downstream, as expected.
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(0U, response->body().size());
}

// Basic test that instantiates a custom upstream local address selector.
TEST_P(HttpProtocolIntegrationTest, CustomUpstreamLocalAddressSelector) {

  std::shared_ptr<size_t> num_calls = std::make_shared<size_t>(0);
  TestUpstreamLocalAddressSelectorFactory factory(num_calls, true);
  Registry::InjectFactory<UpstreamLocalAddressSelectorFactory> registration(factory);

  // Create a config that is invalid for the ``DefaultLocalAddressSelector``.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    uint32_t const port_value = 1234;
    auto bind_config = bootstrap.mutable_cluster_manager()->mutable_upstream_bind_config();
    auto local_address_selector_config = bind_config->mutable_local_address_selector();
    ProtobufWkt::Empty empty;
    local_address_selector_config->mutable_typed_config()->PackFrom(empty);
    local_address_selector_config->set_name("mock.upstream.local.address.selector");
    bind_config->mutable_source_address()->set_address("::1");
    bind_config->mutable_source_address()->set_port_value(port_value);
    auto extra_source_address = bind_config->add_extra_source_addresses();
    extra_source_address->mutable_address()->set_address("::1");
    extra_source_address->mutable_address()->set_port_value(port_value);
    auto extra_source_address2 = bind_config->add_extra_source_addresses();
    extra_source_address2->mutable_address()->set_address("::2");
    extra_source_address2->mutable_address()->set_port_value(port_value);
  });

  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  // Send the request headers from the client, wait until they are received upstream. When they
  // are received, send the default response headers from upstream and wait until they are
  // received at by client
  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  // Verify that we called ``getUpstreamLocalAddressImpl`` on ``TestUpstreamLocalAddressSelector``.
  EXPECT_EQ(*num_calls, 1);

  // Verify the proxied request was received upstream, as expected.
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  // Verify the proxied response was received downstream, as expected.
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(0U, response->body().size());
}

// Verify that the bind config specified on the cluster config overrides the one
// on the cluster manager.
TEST_P(HttpProtocolIntegrationTest, BindConfigOverride) {

  // Set up custom local address selector factory
  std::shared_ptr<size_t> num_calls = std::make_shared<size_t>(0);
  TestUpstreamLocalAddressSelectorFactory factory(num_calls, false);
  Registry::InjectFactory<UpstreamLocalAddressSelectorFactory> registration(factory);
  setUpstreamCount(2);
  uint32_t const port_value_0 = 1234;
  uint32_t const port_value_1 = 12345;

  config_helper_.addConfigModifier([this, port_value_0 = port_value_0, port_value_1 = port_value_1](
                                       envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Specify bind config on the bootstrap with default address selector.
    auto bootstrap_bind_config =
        bootstrap.mutable_cluster_manager()->mutable_upstream_bind_config();
    bootstrap_bind_config->mutable_source_address()->set_address("127.0.0.1");
    bootstrap_bind_config->mutable_source_address()->set_port_value(port_value_0);
    auto bootstrap_extra_source_address = bootstrap_bind_config->add_extra_source_addresses();
    bootstrap_extra_source_address->mutable_address()->set_address("::1");
    bootstrap_extra_source_address->mutable_address()->set_port_value(port_value_0);
    auto bootstrap_address_selector_config =
        bootstrap_bind_config->mutable_local_address_selector();
    envoy::config::upstream::local_address_selector::v3::DefaultLocalAddressSelector config;
    bootstrap_address_selector_config->mutable_typed_config()->PackFrom(config);
    bootstrap_address_selector_config->set_name(
        "envoy.upstream.local_address_selector.default_local_address_selector");

    // Specify a cluster with bind config.
    bootstrap.mutable_static_resources()->mutable_clusters()->Add()->MergeFrom(
        *bootstrap.mutable_static_resources()->mutable_clusters(0));
    bootstrap.mutable_static_resources()->mutable_clusters(1)->set_name("cluster_1");
    auto bind_config =
        bootstrap.mutable_static_resources()->mutable_clusters(1)->mutable_upstream_bind_config();
    bind_config->mutable_source_address()->set_address(
        version_ == Network::Address::IpVersion::v4 ? "127.0.0.2" : "::1");
    bind_config->mutable_source_address()->set_port_value(port_value_1);
    ProtobufWkt::Empty empty;
    auto address_selector_config = bind_config->mutable_local_address_selector();
    address_selector_config->mutable_typed_config()->PackFrom(empty);
    address_selector_config->set_name("test.upstream.local.address.selector");
  });

  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void {
        auto default_route =
            hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
        default_route->mutable_route()->set_cluster("cluster_0");
        default_route->mutable_match()->set_prefix("/path1");

        // Add route that should direct to cluster with custom bind config.
        auto second_route =
            hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes()->Add();
        second_route->mutable_route()->set_cluster("cluster_1");
        second_route->mutable_match()->set_prefix("/path2");
      });

  initialize();

  // Send request to cluster_0
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/path1/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"}};
  auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
  waitForNextUpstreamRequest();

  std::string address_string;
  if (version_ == Network::Address::IpVersion::v4) {
    address_string = "127.0.0.1";
  } else {
    address_string = "::1";
  }
  std::string address_cluster0 = fake_upstream_connection_->connection()
                                     .connectionInfoProvider()
                                     .remoteAddress()
                                     ->ip()
                                     ->addressAsString();
  size_t port_value_cluster0 = fake_upstream_connection_->connection()
                                   .connectionInfoProvider()
                                   .remoteAddress()
                                   ->ip()
                                   ->port();
  EXPECT_EQ(address_cluster0, address_string);
  EXPECT_EQ(port_value_cluster0, port_value_0);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(response->waitForEndStream());

  // Verify the proxied request was received upstream, as expected.
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  // Verify the proxied response was received downstream, as expected.
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(512, response->body().size());

  auto first_connection = std::move(fake_upstream_connection_);
  codec_client_->close();

  // Verify that the custom local address selector was not invoked.
  EXPECT_EQ(*num_calls, 0);

  // Send request to cluster_1.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl request_headers2{{":method", "GET"},
                                                  {":path", "/path2/long/url"},
                                                  {":scheme", "http"},
                                                  {":authority", "host"}};
  response = codec_client_->makeRequestWithBody(request_headers2, 1024);
  waitForNextUpstreamRequest(1);

  if (version_ == Network::Address::IpVersion::v4) {
    address_string = "127.0.0.2";
  } else {
    address_string = "::1";
  }
  std::string address_cluster1 = fake_upstream_connection_->connection()
                                     .connectionInfoProvider()
                                     .remoteAddress()
                                     ->ip()
                                     ->addressAsString();
  size_t port_value_cluster1 = fake_upstream_connection_->connection()
                                   .connectionInfoProvider()
                                   .remoteAddress()
                                   ->ip()
                                   ->port();
  EXPECT_EQ(address_cluster1, address_string);
  EXPECT_EQ(port_value_cluster1, port_value_1);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);
  ASSERT_TRUE(response->waitForEndStream());

  // Verify that we called ``getUpstreamLocalAddressImpl`` on ``TestUpstreamLocalAddressSelector``.
  EXPECT_EQ(*num_calls, 1);

  // Verify the proxied request was received upstream, as expected.
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024, upstream_request_->bodyLength());
  // Verify the proxied response was received downstream, as expected.
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(512, response->body().size());
}

INSTANTIATE_TEST_SUITE_P(Protocols, HttpProtocolIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1}, {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);
} // namespace Upstream
} // namespace Envoy
