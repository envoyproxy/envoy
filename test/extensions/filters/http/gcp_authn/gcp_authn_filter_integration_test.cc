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
    "eyJhbGciOiJSUzI1NiIsImtpZCI6ImYxMzM4Y2EyNjgzNTg2M2Y2NzE0MDhmNDE3MzhhN2I0OWU3NDBmYzAiLCJ0eXAiO";

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
        audience.mutable_audience_map()->insert(
            {std::string(AudienceKey), std::string(AudienceValue)});

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

  void initiateClientConnection() {
    // Create a client aimed at Envoy’s default HTTP port.
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
    Http::TestRequestHeaderMapImpl headers{
        {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
    response_ = codec_client_->makeHeaderOnlyRequest(headers);
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
  }

  // First cluster (i.e., cluster_0) is destination upstream cluster
  void sendRequestToFirstClusterAndValidateResponse(bool with_audience) {
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
      // Verify the request header modification: the token returned from authentication server
      // has been added to the request header that is sent to destination upstream.
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
  )EOF";
  envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig proto_config_{};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, GcpAuthnFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(GcpAuthnFilterIntegrationTest, Basicflow) {
  initializeConfig(/*add_audience=*/true);
  HttpIntegrationTest::initialize();
  initiateClientConnection();

  // Send the request to cluster `gcp_authn`.
  waitForGcpAuthnServerResponse();

  // Send the request to cluster `cluster_0` and validate the response.
  sendRequestToFirstClusterAndValidateResponse(/*with_audience=*/true);

  // Verify request has been routed to both upstream clusters.
  EXPECT_GE(test_server_->counter("cluster.gcp_authn.upstream_cx_total")->value(), 1);
  EXPECT_GE(test_server_->counter("cluster.cluster_0.upstream_cx_total")->value(), 1);

  // Perform the clean-up.
  cleanupUpstreamAndDownstream();
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
  sendRequestToFirstClusterAndValidateResponse(/*with_audience=*/false);

  // Verify request has been routed to `cluster_0` but not `gcp_authn` cluster.
  EXPECT_GE(test_server_->counter("cluster.gcp_authn.upstream_cx_total")->value(), 0);
  EXPECT_GE(test_server_->counter("cluster.cluster_0.upstream_cx_total")->value(), 1);

  // Perform the clean-up.
  cleanupUpstreamAndDownstream();
}

} // namespace
} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
