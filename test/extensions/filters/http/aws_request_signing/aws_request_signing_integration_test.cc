// #include <chrono>

#include "source/common/common/logger.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {
namespace {

const std::string AWS_REQUEST_SIGNING_CONFIG_SIGV4 = R"EOF(
name: envoy.filters.http.aws_request_signing
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.aws_request_signing.v3.AwsRequestSigning
  service_name: vpc-lattice-svcs
  region: us-east-1
  signing_algorithm: aws_sigv4
  use_unsigned_payload: true
  match_excluded_headers:
  - prefix: x-envoy
  - prefix: x-forwarded
  - exact: x-amzn-trace-id
)EOF";

const std::string AWS_REQUEST_SIGNING_CONFIG_SIGV4A = R"EOF(
name: envoy.filters.http.aws_request_signing
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.aws_request_signing.v3.AwsRequestSigning
  service_name: vpc-lattice-svcs
  region: '*'
  signing_algorithm: aws_sigv4a
  use_unsigned_payload: true
  match_excluded_headers:
  - prefix: x-envoy
  - prefix: x-forwarded
  - exact: x-amzn-trace-id
)EOF";

using Headers = std::vector<std::pair<const std::string, const std::string>>;

class AwsRequestSigningIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                         public HttpIntegrationTest {
public:
  AwsRequestSigningIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {
    skipPortUsageValidation();
    // set some environment credentials so the test cases perform signing correctly
    TestEnvironment::setEnvVar("AWS_ACCESS_KEY_ID", "akid", 1);
    TestEnvironment::setEnvVar("AWS_SECRET_ACCESS_KEY", "secret", 1);
    TestEnvironment::setEnvVar("AWS_SESSION_TOKEN", "token", 1);
    TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
    TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
    TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);
    TestEnvironment::setEnvVar("AWS_CONTAINER_CREDENTIALS_FULL_URI", "http://host/path/to/creds",
                               1);
    TestEnvironment::setEnvVar("AWS_CONTAINER_AUTHORIZATION_TOKEN", "auth_token", 1);
  }

  ~AwsRequestSigningIntegrationTest() override {
    TestEnvironment::unsetEnvVar("AWS_ACCESS_KEY_ID");
    TestEnvironment::unsetEnvVar("AWS_SECRET_ACCESS_KEY");
    TestEnvironment::unsetEnvVar("AWS_SESSION_TOKEN");
    TestEnvironment::unsetEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE");
    TestEnvironment::unsetEnvVar("AWS_ROLE_ARN");
    TestEnvironment::unsetEnvVar("AWS_ROLE_SESSION_NAME");
    TestEnvironment::unsetEnvVar("AWS_CONTAINER_CREDENTIALS_FULL_URI");
    TestEnvironment::unsetEnvVar("AWS_CONTAINER_AUTHORIZATION_TOKEN");
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  void addUpstreamProtocolOptions() {
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);

      ConfigHelper::HttpProtocolOptions protocol_options;
      protocol_options.mutable_upstream_http_protocol_options()->set_auto_sni(true);
      protocol_options.mutable_upstream_http_protocol_options()->set_auto_san_validation(true);
      protocol_options.mutable_explicit_http_config()->mutable_http_protocol_options();
      ConfigHelper::setProtocolOptions(*cluster, protocol_options);
    });
  }

protected:
  bool downstream_filter_ = true;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AwsRequestSigningIntegrationTest,
                         testing::ValuesIn({Network::Address::IpVersion::v4}));

TEST_P(AwsRequestSigningIntegrationTest, SigV4IntegrationDownstream) {

  config_helper_.prependFilter(AWS_REQUEST_SIGNING_CONFIG_SIGV4, true);
  HttpIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/path"}, {":scheme", "http"}, {":authority", "host"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  // check that our headers have been correctly added upstream
  EXPECT_FALSE(upstream_request_->headers().get(Http::LowerCaseString("authorization")).empty());
  EXPECT_FALSE(upstream_request_->headers().get(Http::LowerCaseString("x-amz-date")).empty());
  EXPECT_FALSE(
      upstream_request_->headers().get(Http::LowerCaseString("x-amz-security-token")).empty());
  EXPECT_FALSE(
      upstream_request_->headers().get(Http::LowerCaseString("x-amz-content-sha256")).empty());
}

TEST_P(AwsRequestSigningIntegrationTest, SigV4AIntegrationDownstream) {

  config_helper_.prependFilter(AWS_REQUEST_SIGNING_CONFIG_SIGV4A, true);
  HttpIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/path"}, {":scheme", "http"}, {":authority", "host"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  // check that our headers have been correctly added upstream
  EXPECT_FALSE(upstream_request_->headers().get(Http::LowerCaseString("authorization")).empty());
  EXPECT_FALSE(upstream_request_->headers().get(Http::LowerCaseString("x-amz-date")).empty());
  EXPECT_FALSE(upstream_request_->headers().get(Http::LowerCaseString("x-amz-region-set")).empty());
  EXPECT_FALSE(
      upstream_request_->headers().get(Http::LowerCaseString("x-amz-security-token")).empty());
  EXPECT_FALSE(
      upstream_request_->headers().get(Http::LowerCaseString("x-amz-content-sha256")).empty());
}

TEST_P(AwsRequestSigningIntegrationTest, SigV4IntegrationUpstream) {

  config_helper_.prependFilter(AWS_REQUEST_SIGNING_CONFIG_SIGV4, false);
  addUpstreamProtocolOptions();
  HttpIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/path"}, {":scheme", "http"}, {":authority", "host"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  // check that our headers have been correctly added upstream
  EXPECT_FALSE(upstream_request_->headers().get(Http::LowerCaseString("authorization")).empty());
  EXPECT_FALSE(upstream_request_->headers().get(Http::LowerCaseString("x-amz-date")).empty());
  EXPECT_FALSE(
      upstream_request_->headers().get(Http::LowerCaseString("x-amz-security-token")).empty());
  EXPECT_FALSE(
      upstream_request_->headers().get(Http::LowerCaseString("x-amz-content-sha256")).empty());
}

TEST_P(AwsRequestSigningIntegrationTest, SigV4AIntegrationUpstream) {

  config_helper_.prependFilter(AWS_REQUEST_SIGNING_CONFIG_SIGV4A, false);
  addUpstreamProtocolOptions();
  HttpIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/path"}, {":scheme", "http"}, {":authority", "host"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  // check that our headers have been correctly added upstream
  EXPECT_FALSE(upstream_request_->headers().get(Http::LowerCaseString("authorization")).empty());
  EXPECT_FALSE(upstream_request_->headers().get(Http::LowerCaseString("x-amz-date")).empty());
  EXPECT_FALSE(upstream_request_->headers().get(Http::LowerCaseString("x-amz-region-set")).empty());
  EXPECT_FALSE(
      upstream_request_->headers().get(Http::LowerCaseString("x-amz-security-token")).empty());
  EXPECT_FALSE(
      upstream_request_->headers().get(Http::LowerCaseString("x-amz-content-sha256")).empty());
}

const std::string SIMPLE_CONFIG = R"EOF(
layered_runtime:
  layers:
  - name: static_layer
    static_layer:
      envoy.reloadable_features.use_http_client_to_fetch_aws_credentials: true

static_resources:
  listeners:
  - address:
      socket_address:
        address: 0.0.0.0
        port_value: 8080
    filter_chains:
    - filters:
      - name: envoy.filters.network.http_connection_manager
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
          codec_type: AUTO
          stat_prefix: ingress_http
          route_config:
            name: local_route
            virtual_hosts:
            - name: app
              domains:
              - "*"
              routes:
              - match:
                  prefix: "/"
                route:
                  cluster: versioned-cluster
          http_filters:
          - name: envoy.filters.http.aws_request_signing
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.aws_request_signing.v3.AwsRequestSigning
              service_name: s3
              region: us-west-2
              use_unsigned_payload: true
              match_excluded_headers:
              - prefix: x-envoy
              - prefix: x-forwarded
              - exact: x-amzn-trace-id
          - name: envoy.filters.http.router
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router

  clusters:
  - name: versioned-cluster
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: versioned-cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 8000
)EOF";

class InitializeFilterTest : public ::testing::Test, public HttpIntegrationTest {
public:
  InitializeFilterTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, TestEnvironment::getIpVersionsForTest().front(),
                            ConfigHelper::httpProxyConfig()) {}
};

TEST_F(InitializeFilterTest, First) {
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);

  config_helper_.prependFilter(AWS_REQUEST_SIGNING_CONFIG_SIGV4, true);
  HttpIntegrationTest::initialize();

  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  EXPECT_EQ(
      2, test_server_->gauge("thread_local_cluster_manager.worker_0.clusters_inflated")->value());
}

} // namespace
} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
