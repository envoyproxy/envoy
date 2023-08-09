#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "envoy/upstream/upstream.h"

#include "test/integration/http_protocol_integration.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Upstream {

// Basic test that instantiates the DefaultLocalAddressSelector.
TEST_P(HttpProtocolIntegrationTest, Basic) {

  // Specify both IPv4 and IPv6 source addresses, so the right one is picked
  // based on IP version of upstream.
  auto const port_value = 1234;
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
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

TEST_P(HttpProtocolIntegrationTest, CustomUpstreamLocalAddressSelector) {

  ::testing::StrictMock<MockUpstreamLocalAddressSelectorFactory> factory;
  Registry::InjectFactory<UpstreamLocalAddressSelectorFactory> register_factory(factory);

  Network::Address::InstanceConstSharedPtr source_address;
  std::shared_ptr<::testing::StrictMock<MockUpstreamLocalAddressSelector>> local_address_selector =
      std::make_shared<::testing::StrictMock<MockUpstreamLocalAddressSelector>>(source_address);
  // Setup expectations.
  EXPECT_CALL(factory, createLocalAddressSelector(_, _))
      .WillOnce(::testing::Return(local_address_selector));
  EXPECT_CALL(*local_address_selector, getUpstreamLocalAddressImpl(_))
      .WillOnce(::testing::Return(UpstreamLocalAddress()));

  auto const port_value = 1234;
  // Create a config that is invalid for the DefaultLocalAddressSelector.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
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

  // Verify the proxied request was received upstream, as expected.
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  // Verify the proxied response was received downstream, as expected.
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(0U, response->body().size());
}

INSTANTIATE_TEST_SUITE_P(Protocols, HttpProtocolIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1}, {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);
} // namespace Upstream
} // namespace Envoy
