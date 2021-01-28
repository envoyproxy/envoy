#include "common/protobuf/message_validator_impl.h"
#include "common/protobuf/utility.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {
namespace {

class PrevioustHostsIntegrationTest : public testing::Test, public HttpIntegrationTest {
public:
  PrevioustHostsIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, Network::Address::IpVersion::v4) {}

  void initialize() override {
    setDeterministic();

    // Add the retry configuration to a new virtual host.
    const auto vhost_config = R"EOF(
name: retry_service
domains: ["retry"]
routes:
- match:
    prefix: "/"
  route:
    cluster: cluster_0
    retry_policy:
      retry_on: 5xx
      retry_host_predicate:
      - name: envoy.retry_host_predicates.previous_hosts
)EOF";

    envoy::config::route::v3::VirtualHost virtual_host;
    MessageUtil::loadFromYamlAndValidate(vhost_config, virtual_host,
                                         ProtobufMessage::getStrictValidationVisitor());

    config_helper_.addVirtualHost(virtual_host);

    // cluster_0 should have two endpoints so we can observe retry attempts against them.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      cluster->mutable_load_assignment()->mutable_endpoints(0)->add_lb_endpoints()->MergeFrom(
          cluster->load_assignment().endpoints(0).lb_endpoints(0));
    });

    setUpstreamCount(2);

    HttpIntegrationTest::initialize();
  }
};

TEST_F(PrevioustHostsIntegrationTest, BasicFlow) {
  initialize();

  // Testing that the extension works is a bit tricky: We have a cluster with two endpoints, and
  // want to ensure that the second attempt is not routed to the same upstream. The difficulty
  // arises from the fact that naively this would already be the case when using RR: the next host
  // would be selected for the retry. While we could try to search for a random seed that would make
  // RANDOM hit the same host twice, we instead opt for some careful sequencing of requests: Issue
  // the first request which should fail, but before it does so issue another request that succeeds.
  // This should mean that the next host to hit for the retry of the original request would be the
  // same as the host targeted. We can then verify that we *don't* route to this host, due to retry
  // plugin rejecting the first host selection.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"}, {":path", "/whatever"}, {":scheme", "http"}, {":authority", "retry"}};
  auto first_response = codec_client_->makeRequestWithBody(headers, "");

  waitForNextUpstreamRequest(0);

  // Hold onto this so we can respond to it later.
  auto first_upstream_request = std::move(upstream_request_);
  auto first_upstream_connection = std::move(fake_upstream_connection_);

  // Issue another request to tickle the RR selection logic.
  {
    Http::TestRequestHeaderMapImpl headers{
        {":method", "GET"}, {":path", "/whatever"}, {":scheme", "http"}, {":authority", "retry"}};
    auto response = codec_client_->makeRequestWithBody(headers, "");
    waitForNextUpstreamRequest(1);
    upstream_request_->encodeHeaders(default_response_headers_, true);
    response->waitForEndStream();
  }

  // Now respond 500 to the original request to trigger a retry.
  Http::TestResponseHeaderMapImpl error_headers{{":status", "500"}};
  first_upstream_request->encodeHeaders(error_headers, true);

  // Should get routed to upstream 1 due to the extension.
  waitForNextUpstreamRequest(1);
  upstream_request_->encodeHeaders(default_response_headers_, true);

  // Since the retry succeeded we end up returning 200.
  first_response->waitForHeaders();
  EXPECT_EQ("200", first_response->headers().getStatusValue());
}

} // namespace
} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
