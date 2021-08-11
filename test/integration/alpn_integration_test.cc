#include "test/integration/autonomous_upstream.h"
#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class AlpnIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                            public HttpIntegrationTest {
public:
  AlpnIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {}

  void SetUp() override {
    autonomous_upstream_ = true;
    setUpstreamCount(2);
    setDownstreamProtocol(Http::CodecType::HTTP2);

    upstream_tls_ = true;
    config_helper_.configureUpstreamTls(true);
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* static_resources = bootstrap.mutable_static_resources();
      auto* cluster = static_resources->mutable_clusters(0);
      auto* load_assignment = cluster->mutable_load_assignment();
      load_assignment->set_cluster_name(cluster->name());
      auto* locality = load_assignment->add_endpoints();
      locality->set_priority(0);
      locality->mutable_locality()->set_region("region");
      locality->add_lb_endpoints()->mutable_endpoint()->MergeFrom(
          ConfigHelper::buildEndpoint(Network::Test::getLoopbackAddressString(version_)));
    });
  }
  void createUpstreams() override {
    for (uint32_t i = 0; i < fake_upstreams_count_; ++i) {
      auto config = upstreamConfig();
      config.upstream_protocol_ = protocols_[i];
      Network::TransportSocketFactoryPtr factory = createUpstreamTlsContext(config);
      auto endpoint = upstream_address_fn_(i);
      fake_upstreams_.emplace_back(new AutonomousUpstream(std::move(factory), endpoint, config,
                                                          autonomous_allow_incomplete_streams_));
    }
  }
  std::vector<Http::CodecType> protocols_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AlpnIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AlpnIntegrationTest, Http2) {
  setUpstreamProtocol(Http::CodecType::HTTP2);
  protocols_ = {Http::CodecType::HTTP2, Http::CodecType::HTTP2};
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
}

TEST_P(AlpnIntegrationTest, Http1) {
  setUpstreamProtocol(Http::CodecType::HTTP1);
  protocols_ = {Http::CodecType::HTTP1, Http::CodecType::HTTP1};
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
}

TEST_P(AlpnIntegrationTest, Mixed) {
  protocols_ = {Http::CodecType::HTTP1, Http::CodecType::HTTP2};
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  // Kick off two simultaneous requests, to ensure two upstream connections are
  // created.
  auto encoder_decoder1 = codec_client_->startRequest(default_request_headers_);
  auto& encoder1 = encoder_decoder1.first;
  auto& response1 = encoder_decoder1.second;

  auto encoder_decoder2 = codec_client_->startRequest(default_request_headers_);
  auto& encoder2 = encoder_decoder2.first;
  auto& response2 = encoder_decoder2.second;

  // Finish both streams to ensure both responses come through.
  Buffer::OwnedImpl data("");
  encoder1.encodeData(data, true);
  encoder2.encodeData(data, true);

  ASSERT_TRUE(response1->waitForEndStream());
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ("200", response1->headers().Status()->value().getStringView());
  EXPECT_EQ("200", response2->headers().Status()->value().getStringView());
}

} // namespace
} // namespace Envoy
