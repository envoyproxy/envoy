#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.validate.h"

#include "source/extensions/filters/http/gcp_authn/gcp_authn_filter.h"

#include "test/integration/http_integration.h"
#include "test/mocks/server/options.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {
namespace {

constexpr absl::string_view AudienceValue = "http://test.com";
constexpr absl::string_view Url = "http://metadata.google.internal/computeMetadata/v1/instance/"
                                  "service-accounts/default/identity?audience=[AUDIENCE]";
constexpr absl::string_view MockTokenString =
    "eyJhbGciOiJSUzI1NiIsImtpZCI6ImIxYTgyNTllYjA3NjYwZWYyMzc4MWM4NWI3ODQ5YmZhMGExYzgwNmMiLCJ0eXAiOi"
    "JKV1QifQ."
    "eyJhdWQiOiJ3d3cuZ29vZ2xlLmNvbSIsImF6cCI6IjEwNjI3NTM0NDEzNzgyODM4MDAwOSIsImV4cCI6MTY1MjM4MjA3MS"
    "wiaWF0IjoxNjUyMzc4NDcxLCJpc3MiOiJodHRwczovL2FjY291bnRzLmdvb2dsZS5jb20iLCJzdWIiOiIxMDYyNzUzNDQx"
    "Mzc4MjgzODAwMDkifQ.OFAt_5aJGWs4JI4SBvs_Exhhra6si9d5W__4pSAzK7YXLA_JUuX46YWTfw6E_"
    "5c2FvHEzGbgZkGRvuFaTtZebXALAzhpAgYpqVwWg5URI1dkjRG53kQD9dxw3IatT1xryXQP-"
    "MONOYOaybMzbTIfbEbItRAs3ShZ32ZjQpw-pr-"
    "om80vnCN78uBlsk4mstgI3RjhWDcvJ1Hc7UJW9QPpDOigfn9SGV9p1bjGdr9imv-"
    "Ny1oEG72xKhYdKTYAxJCYB8I1Yh3hUL8SU43OxHqpaRJ_Sr680FnKgjXjIRL9sBeu_D8-"
    "jkaDD39vBNKlQvlZm7p6qpOgCy0j_TxYABFQ-A";

class GcpAuthnFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                      public HttpIntegrationTest {
public:
  GcpAuthnFilterIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {}

  void createUpstreams() override {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
    //  Add two fake upstreams, the second one is for gcp authentication stream.
    for (int i = 0; i < 2; ++i) {
      addFakeUpstream(FakeHttpConnection::Type::HTTP2);
    }
  }

  void initializeConfig(bool add_audience) {
    config_helper_.addConfigModifier([this, add_audience](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* gcp_authn_cluster = bootstrap.mutable_static_resources()->add_clusters();
      gcp_authn_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      gcp_authn_cluster->set_name("gcp_authn");
      gcp_authn_cluster->mutable_load_assignment()->set_cluster_name("gcp_authn");
      ConfigHelper::setHttp2(*gcp_authn_cluster);

      if (add_audience) {
        // Add the metadata to cluster 0 (destination cluster) configuration. The audience (URL of
        // the destination cluster) is provided through the metadata.
        auto cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters(0);
        envoy::config::core::v3::Metadata* cluster_metadata = cluster_0->mutable_metadata();
        envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
        audience.set_url(std::string(AudienceValue));
        (*cluster_metadata->mutable_typed_filter_metadata())
            [std::string(Envoy::Extensions::HttpFilters::GcpAuthn::FilterName)]
                .PackFrom(audience);
      }

      // Set URI in the config and create the filter.
      TestUtility::loadFromYaml(default_config_, proto_config_);
      auto& uri = *proto_config_.mutable_http_uri();
      uri.set_uri(std::string(Url));
      envoy::config::listener::v3::Filter gcp_authn_filter;
      gcp_authn_filter.set_name(std::string(Envoy::Extensions::HttpFilters::GcpAuthn::FilterName));
      gcp_authn_filter.mutable_typed_config()->PackFrom(proto_config_);

      // Add the filter to the filter chain.
      config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrDie(gcp_authn_filter));
    });
  }

  void initiateClientConnection(bool send_request_body = false) {
    // Create a client aimed at Envoy’s default HTTP port.
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
    if (send_request_body) {
      response_ = codec_client_->makeRequestWithBody(
          Http::TestRequestHeaderMapImpl{
              {":method", "POST"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}},
          "test");
    } else {
      response_ = codec_client_->makeHeaderOnlyRequest(
          Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":path", "/"},
                                         {":scheme", "http"},
                                         {":authority", "host"},
                                         // Add a pair with `Authorization` as the key for
                                         // verification of header map overridden behavior.
                                         {"Authorization", "test"}});
    }
  }

  void waitForGcpAuthnServerResponse() {
    AssertionResult result =
        fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_gcp_authn_connection_);
    RELEASE_ASSERT(result, result.message());
    result = fake_gcp_authn_connection_->waitForNewStream(*dispatcher_, request_);
    RELEASE_ASSERT(result, result.message());

    std::string final_url = absl::StrReplaceAll(Url, {{"[AUDIENCE]", AudienceValue}});
    absl::string_view host;
    absl::string_view path;
    Envoy::Http::Utility::extractHostPathFromUri(final_url, host, path);
    // Need to wait for headers complete before reading headers value.
    result = request_->waitForHeadersComplete();
    RELEASE_ASSERT(result, result.message());
    // Verify that request was constructed correctly at filter and then the request was sent to
    // fake upstream successfully from http async client.
    EXPECT_EQ(request_->headers().Host()->value(), std::string(host));
    EXPECT_EQ(request_->headers().Path()->value(), std::string(path));
    // Send response headers with end_stream false because we want to add response body next.
    request_->encodeHeaders(default_response_headers_, false);
    // Send response data with end_stream true.
    request_->encodeData(MockTokenString, true);
    result = request_->waitForEndStream(*dispatcher_);
    RELEASE_ASSERT(result, result.message());
    // Verify the proxied request was received upstream, as expected.
    EXPECT_TRUE(request_->complete());
  }

  // Send the request to destination upstream cluster
  void sendRequestToDestinationAndValidateResponse(bool with_audience) {
    // Send the request to cluster `cluster_0`;
    AssertionResult result =
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_);
    RELEASE_ASSERT(result, result.message());
    result = fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_);
    RELEASE_ASSERT(result, result.message());
    result = upstream_request_->waitForEndStream(*dispatcher_);
    RELEASE_ASSERT(result, result.message());
    // Send response headers, and end_stream if there is no response body.
    upstream_request_->encodeHeaders(default_response_headers_, true);
    // Wait for the response to be read by the codec client.
    RELEASE_ASSERT(response_->waitForEndStream(TestUtility::DefaultTimeout), "unexpected timeout");

    // Verify the proxied request was received upstream, as expected.
    EXPECT_TRUE(upstream_request_->complete());
    // The authorization header is only added when the configuration is valid (e.g., with audience
    // field).
    if (with_audience) {
      ASSERT_FALSE(upstream_request_->headers().get(authorizationHeaderKey()).empty());
      // The expected ID token is in format of `Bearer ID_TOKEN`
      std::string id_token = absl::StrCat("Bearer ", MockTokenString);
      // Verify the request header modification:
      // 1) Only one entry with authorization header key. i.e., Any existing values should be
      // overridden by response from authentication server.
      EXPECT_EQ(upstream_request_->headers().get(authorizationHeaderKey()).size(), 1);
      // 2) the token returned from authentication server has been added to the request header that
      // is sent to destination upstream.
      EXPECT_EQ(
          upstream_request_->headers().get(authorizationHeaderKey())[0]->value().getStringView(),
          id_token);
    }

    EXPECT_EQ(0U, upstream_request_->bodyLength());
    // Verify the proxied response was received downstream, as expected.
    EXPECT_TRUE(response_->complete());
    EXPECT_EQ("200", response_->headers().getStatusValue());
    EXPECT_EQ(0U, response_->body().size());
  }

  // Perform the clean-up.
  void cleanup() {
    if (fake_gcp_authn_connection_ != nullptr) {
      AssertionResult result = fake_gcp_authn_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = fake_gcp_authn_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      fake_gcp_authn_connection_.reset();
    }
    // Close |codec_client_| and |fake_upstream_connection_| cleanly.
    cleanupUpstreamAndDownstream();
  }

  IntegrationStreamDecoderPtr response_;
  IntegrationStreamDecoderPtr gcp_response_;
  FakeHttpConnectionPtr fake_gcp_authn_connection_{};
  FakeStreamPtr request_{};
  const std::string default_config_ = R"EOF(
    http_uri:
      uri: "gcp_authn:9000"
      cluster: gcp_authn
      timeout:
        seconds: 5
    cache_config:
      cache_size: 100
  )EOF";
  envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig proto_config_{};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, GcpAuthnFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(GcpAuthnFilterIntegrationTest, Basicflow) {
  initializeConfig(/*add_audience=*/true);
  HttpIntegrationTest::initialize();
  int num = 2;
  // Send multiple requests.
  for (int i = 0; i < num; ++i) {
    initiateClientConnection();
    // Send the request to cluster `gcp_authn`.
    waitForGcpAuthnServerResponse();
    // Send the request to cluster `cluster_0` and validate the response.
    sendRequestToDestinationAndValidateResponse(/*with_audience=*/true);
    // Clean up the codec and connections.
    cleanup();
  }

  // Verify request has been routed to both upstream clusters.
  EXPECT_GE(test_server_->counter("cluster.gcp_authn.upstream_cx_total")->value(), num);
  EXPECT_GE(test_server_->counter("cluster.cluster_0.upstream_cx_total")->value(), num);
}

TEST_P(GcpAuthnFilterIntegrationTest, BasicflowWithoutAudience) {
  initializeConfig(/*add_audience=*/false);
  HttpIntegrationTest::initialize();
  initiateClientConnection();

  // Sending the request to cluster `gcp_authn` is expected to be failed because no audience in the
  // configuration.
  EXPECT_FALSE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_gcp_authn_connection_,
                                                         std::chrono::milliseconds(200)));

  // Send the request to cluster `cluster_0` and validate the response.
  sendRequestToDestinationAndValidateResponse(/*with_audience=*/false);

  // Verify request has been routed to `cluster_0` but not `gcp_authn` cluster.
  EXPECT_GE(test_server_->counter("cluster.gcp_authn.upstream_cx_total")->value(), 0);
  EXPECT_GE(test_server_->counter("cluster.cluster_0.upstream_cx_total")->value(), 1);

  // Clean up the codec and connections.
  cleanup();
}

// This test is sending the request with body to verify that the filter chain iteration which has
// been stopped by `decodeHeader`'s return status will not be resumed by `decodeData`.
TEST_P(GcpAuthnFilterIntegrationTest, SendRequestWithBody) {
  initializeConfig(/*add_audience=*/true);
  HttpIntegrationTest::initialize();
  initiateClientConnection(/*send_request_body=*/true);
  // Send the request with long wait time to intentionally delay the response from `gcp_authn`
  // cluster.
  EXPECT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_gcp_authn_connection_,
                                                        std::chrono::milliseconds(500000)));
  // Send the request to `cluster_0` cluster.
  AssertionResult assert_result = fake_upstreams_[0]->waitForHttpConnection(
      *dispatcher_, fake_upstream_connection_, std::chrono::milliseconds(1000));
  // We expect the request fail to arrive at `cluster_0` because the filter chain iteration
  // should has already been stopped by waiting for the response from `gcp_authn` cluster above.
  RELEASE_ASSERT(!assert_result, assert_result.message());
  // Clean up the codec and connections.
  cleanup();
}

} // namespace
} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
