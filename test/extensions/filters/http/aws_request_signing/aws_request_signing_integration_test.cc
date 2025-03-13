#include "envoy/extensions/filters/http/aws_request_signing/v3/aws_request_signing.pb.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/extensions/clusters/logical_dns/logical_dns_cluster.h"

#include "test/common/upstream/utility.h"
#include "test/extensions/common/aws/mocks.h"
#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {
namespace {

using testing::Return;

const std::string AWS_REQUEST_SIGNING_CONFIG_SIGV4 = R"EOF(
name: envoy.filters.http.aws_request_signing
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.aws_request_signing.v3.AwsRequestSigning
  service_name: vpc-lattice-svcs
  region: ap-southeast-2
  signing_algorithm: aws_sigv4
  use_unsigned_payload: true
  match_excluded_headers:
  - prefix: x-envoy
  - prefix: x-forwarded
  - exact: x-amzn-trace-id
)EOF";

const std::string AWS_REQUEST_SIGNING_CONFIG_SIGV4_CUSTOM = R"EOF(
name: envoy.filters.http.aws_request_signing
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.aws_request_signing.v3.AwsRequestSigning
  credential_provider:
    custom_credential_provider_chain: true
    assume_role_with_web_identity_provider:
      web_identity_token_data_source:
        filename: /tmp/a
      role_arn: arn:aws:test
      role_session_name: testing
  service_name: vpc-lattice-svcs
  region: ap-southeast-2
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

const std::string AWS_REQUEST_SIGNING_CONFIG_SIGV4_ROUTE_LEVEL = R"EOF(
aws_request_signing:
  service_name: s3
  region: ap-southeast-2
  use_unsigned_payload: true
  host_rewrite: new-host
  match_excluded_headers:
  - prefix: x-envoy
  - prefix: x-forwarded
  - exact: x-amzn-trace-id
stat_prefix: some-prefix
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
    TestEnvironment::setEnvVar("AWS_CONTAINER_CREDENTIALS_FULL_URI",
                               "http://127.0.0.1/path/to/creds", 1);
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
class MockLogicalDnsClusterFactory : public Upstream::LogicalDnsClusterFactory {
public:
  MockLogicalDnsClusterFactory() = default;
  ~MockLogicalDnsClusterFactory() override = default;

  MOCK_METHOD((absl::StatusOr<std::pair<Upstream::ClusterImplBaseSharedPtr,
                                        Upstream::ThreadAwareLoadBalancerPtr>>),
              CreateClusterImpl,
              (const envoy::config::cluster::v3::Cluster& cluster,
               Upstream::ClusterFactoryContext& context));
};

// These test cases validate that each of the metadata async cluster types perform an initial
// credential refresh This means that future refreshes will continue using the standard timer

class InitializeFilterTest : public ::testing::Test, public HttpIntegrationTest {
public:
  InitializeFilterTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, TestEnvironment::getIpVersionsForTest().front(),
                            ConfigHelper::httpProxyConfig()),
        dns_cluster_factory_(logical_dns_cluster_factory_),
        registered_dns_factory_(dns_resolver_factory_) {
    use_lds_ = false;
  }
  NiceMock<MockLogicalDnsClusterFactory> logical_dns_cluster_factory_;
  Registry::InjectFactory<Envoy::Upstream::LogicalDnsClusterFactory> dns_cluster_factory_;
  NiceMock<Network::MockDnsResolverFactory> dns_resolver_factory_;
  Registry::InjectFactory<Network::DnsResolverFactory> registered_dns_factory_;

  Network::DnsResolver::ResolveCb dns_callback_;
  Network::MockActiveDnsQuery active_dns_query_;
  std::shared_ptr<NiceMock<Network::MockDnsResolver>> dns_resolver_{
      new NiceMock<Network::MockDnsResolver>};
  NiceMock<Event::MockDispatcher> dispatcher_;

  void expectResolve(Network::DnsLookupFamily, const std::string& expected_address) {
    EXPECT_CALL(*dns_resolver_, resolve(expected_address, _, _))
        .WillRepeatedly(Invoke([&](const std::string&, Network::DnsLookupFamily,
                                   Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
          cb(Network::DnsResolver::ResolutionStatus::Completed, "",
             TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));

          return nullptr;
        }));
  }

  void dnsSetup() {
    ON_CALL(dns_resolver_factory_, createDnsResolver(_, _, _)).WillByDefault(Return(dns_resolver_));
    expectResolve(Network::DnsLookupFamily::V4Only, "sts.ap-southeast-2.amazonaws.com");
  }

  void addStandardFilter(bool downstream = true) {
    config_helper_.prependFilter(AWS_REQUEST_SIGNING_CONFIG_SIGV4, downstream);
  }

  void addCustomCredentialChainFilter(bool downstream = true) {
    config_helper_.prependFilter(AWS_REQUEST_SIGNING_CONFIG_SIGV4_CUSTOM, downstream);
  }

  void addPerRouteFilter(const std::string& yaml_config) {

    config_helper_.addConfigModifier(
        [&yaml_config](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                cfg) {
          envoy::extensions::filters::http::aws_request_signing::v3::AwsRequestSigningPerRoute
              per_route_config;
          TestUtility::loadFromYaml(yaml_config, per_route_config);

          auto* config = cfg.mutable_route_config()
                             ->mutable_virtual_hosts()
                             ->Mutable(0)
                             ->mutable_typed_per_filter_config();

          (*config)["envoy.filters.http.aws_request_signing"].PackFrom(per_route_config);
        });
  }

  void addUpstreamProtocolOptions(int index = 0) {
    config_helper_.addConfigModifier(
        [&, index](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(index);

          ConfigHelper::HttpProtocolOptions protocol_options;
          protocol_options.mutable_upstream_http_protocol_options()->set_auto_sni(true);
          protocol_options.mutable_upstream_http_protocol_options()->set_auto_san_validation(true);
          protocol_options.mutable_explicit_http_config()->mutable_http_protocol_options();
          ConfigHelper::setProtocolOptions(*cluster, protocol_options);
        });
  }

  ~InitializeFilterTest() override {
    TestEnvironment::unsetEnvVar("AWS_ACCESS_KEY_ID");
    TestEnvironment::unsetEnvVar("AWS_SECRET_ACCESS_KEY");
    TestEnvironment::unsetEnvVar("AWS_SESSION_TOKEN");
    TestEnvironment::unsetEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE");
    TestEnvironment::unsetEnvVar("AWS_ROLE_ARN");
    TestEnvironment::unsetEnvVar("AWS_ROLE_SESSION_NAME");
    TestEnvironment::unsetEnvVar("AWS_CONTAINER_CREDENTIALS_FULL_URI");
    TestEnvironment::unsetEnvVar("AWS_CONTAINER_CREDENTIALS_RELATIVE_URI");
    TestEnvironment::unsetEnvVar("AWS_CONTAINER_AUTHORIZATION_TOKEN");
    TestEnvironment::unsetEnvVar("AWS_EC2_METADATA_DISABLED");
  }
};

TEST_F(InitializeFilterTest, TestWithOneClusterStandard) {

  // Web Identity Credentials only
  dnsSetup();

  TestEnvironment::setEnvVar("AWS_EC2_METADATA_DISABLED", "true", 1);
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);
  addStandardFilter();

  initialize();

  test_server_->waitForCounterGe("aws.metadata_credentials_provider.sts_token_service_internal-ap-"
                                 "southeast-2.credential_refreshes_performed",
                                 1, std::chrono::seconds(10));
}

TEST_F(InitializeFilterTest, TestWithOneClusterCustomWebIdentity) {

  // Web Identity Credentials only
  dnsSetup();

  TestEnvironment::setEnvVar("AWS_EC2_METADATA_DISABLED", "true", 1);
  addCustomCredentialChainFilter();
  initialize();

  test_server_->waitForCounterGe("aws.metadata_credentials_provider.sts_token_service_internal-ap-"
                                 "southeast-2.credential_refreshes_performed",
                                 1, std::chrono::seconds(10));
}

TEST_F(InitializeFilterTest, TestWithOneClusterStandardUpstream) {

  // Web Identity Credentials only
  dnsSetup();

  TestEnvironment::setEnvVar("AWS_EC2_METADATA_DISABLED", "true", 1);
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);
  addStandardFilter(false);
  addUpstreamProtocolOptions();
  initialize();

  test_server_->waitForCounterGe("aws.metadata_credentials_provider.sts_token_service_internal-ap-"
                                 "southeast-2.credential_refreshes_performed",
                                 1, std::chrono::seconds(10));
}

TEST_F(InitializeFilterTest, TestWithTwoClustersUpstreamCheckForSingletonIMDS) {

  // Instance Profile Credentials only
  dnsSetup();

  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    *bootstrap.mutable_static_resources()->add_clusters() =
        config_helper_.buildStaticCluster("cluster_1", 12345, "127.0.0.1");
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    ConfigHelper::HttpProtocolOptions protocol_options;
    protocol_options.mutable_upstream_http_protocol_options()->set_auto_sni(true);
    protocol_options.mutable_upstream_http_protocol_options()->set_auto_san_validation(true);
    protocol_options.mutable_explicit_http_config()->mutable_http_protocol_options();
    ConfigHelper::setProtocolOptions(*cluster, protocol_options);
    auto* cluster1 = bootstrap.mutable_static_resources()->mutable_clusters(1);
    ConfigHelper::setProtocolOptions(*cluster1, protocol_options);
    addStandardFilter(false);
  });

  initialize();
  // We should see a successful credential refresh
  test_server_->waitForCounterGe("aws.metadata_credentials_provider.ec2_instance_metadata_server_"
                                 "internal.credential_refreshes_performed",
                                 1);
  // If credential refresh has succeeded, then check we added only a single cluster via the
  // extension
  EXPECT_EQ(test_server_->counter("cluster_manager.cluster_added"), 1);
}

TEST_F(InitializeFilterTest, TestWithOneClusterRouteLevel) {
  dnsSetup();
  // Web Identity Credentials only
  TestEnvironment::setEnvVar("AWS_EC2_METADATA_DISABLED", "true", 1);
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);
  addPerRouteFilter(AWS_REQUEST_SIGNING_CONFIG_SIGV4_ROUTE_LEVEL);
  initialize();

  test_server_->waitForCounterGe("aws.metadata_credentials_provider.sts_token_service_internal-ap-"
                                 "southeast-2.credential_refreshes_performed",
                                 1, std::chrono::seconds(10));
}

TEST_F(InitializeFilterTest, TestWithOneClusterRouteLevelAndStandard) {
  dnsSetup();
  // Web Identity Credentials only
  TestEnvironment::setEnvVar("AWS_EC2_METADATA_DISABLED", "true", 1);
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);
  addStandardFilter();
  addPerRouteFilter(AWS_REQUEST_SIGNING_CONFIG_SIGV4_ROUTE_LEVEL);
  initialize();

  test_server_->waitForCounterGe("aws.metadata_credentials_provider.sts_token_service_internal-ap-"
                                 "southeast-2.credential_refreshes_performed",
                                 1, std::chrono::seconds(10));
}

TEST_F(InitializeFilterTest, TestWithTwoClustersStandard) {
  dnsSetup();
  // Web Identity Credentials and Container Credentials
  TestEnvironment::setEnvVar("AWS_EC2_METADATA_DISABLED", "true", 1);
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);
  TestEnvironment::setEnvVar("AWS_CONTAINER_CREDENTIALS_RELATIVE_URI", "/path/to/creds", 1);
  TestEnvironment::setEnvVar("AWS_CONTAINER_AUTHORIZATION_TOKEN", "auth_token", 1);
  addStandardFilter();
  initialize();
  std::vector<Stats::GaugeSharedPtr> gauges = test_server_->gauges();
  test_server_->waitForCounterGe("aws.metadata_credentials_provider.ecs_task_"
                                 "metadata_server_internal.credential_refreshes_performed",
                                 1);

  test_server_->waitForCounterGe("aws.metadata_credentials_provider.sts_token_service_internal-ap-"
                                 "southeast-2.credential_refreshes_performed",
                                 1, std::chrono::seconds(10));
}

TEST_F(InitializeFilterTest, TestWithTwoClustersRouteLevel) {
  dnsSetup();
  // Web Identity Credentials and Container Credentials
  TestEnvironment::setEnvVar("AWS_EC2_METADATA_DISABLED", "true", 1);
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);
  TestEnvironment::setEnvVar("AWS_CONTAINER_CREDENTIALS_RELATIVE_URI", "/path/to/creds", 1);
  TestEnvironment::setEnvVar("AWS_CONTAINER_AUTHORIZATION_TOKEN", "auth_token", 1);
  addPerRouteFilter(AWS_REQUEST_SIGNING_CONFIG_SIGV4_ROUTE_LEVEL);
  initialize();
  test_server_->waitForCounterGe("aws.metadata_credentials_provider.ecs_task_"
                                 "metadata_server_internal.credential_refreshes_performed",
                                 1);

  test_server_->waitForCounterGe("aws.metadata_credentials_provider.sts_token_service_internal-ap-"
                                 "southeast-2.credential_refreshes_performed",
                                 1, std::chrono::seconds(10));
}

TEST_F(InitializeFilterTest, TestWithMultipleWebidentityRouteLevel) {

  ON_CALL(dns_resolver_factory_, createDnsResolver(_, _, _)).WillByDefault(Return(dns_resolver_));

  expectResolve(Network::DnsLookupFamily::V4Only, "sts.ap-southeast-1.amazonaws.com");
  expectResolve(Network::DnsLookupFamily::V4Only, "sts.ap-southeast-2.amazonaws.com");
  expectResolve(Network::DnsLookupFamily::V4Only, "sts.eu-west-1.amazonaws.com");
  expectResolve(Network::DnsLookupFamily::V4Only, "sts.eu-west-2.amazonaws.com");
  expectResolve(Network::DnsLookupFamily::V4Only, "sts.eu-west-3.amazonaws.com");

  // Web Identity Credentials and Container Credentials
  TestEnvironment::setEnvVar("AWS_EC2_METADATA_DISABLED", "true", 1);
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);
  TestEnvironment::setEnvVar("AWS_CONTAINER_CREDENTIALS_RELATIVE_URI", "/path/to/creds", 1);
  TestEnvironment::setEnvVar("AWS_CONTAINER_AUTHORIZATION_TOKEN", "auth_token", 1);

  const std::string route_level_config = R"EOF(
  aws_request_signing:
    service_name: vpc-lattice-svcs
    region: {}
    use_unsigned_payload: true
    host_rewrite: new-host
    match_excluded_headers:
    - prefix: x-envoy
    - prefix: x-forwarded
    - exact: x-amzn-trace-id
  stat_prefix: some-prefix
  )EOF";

  config_helper_.addConfigModifier(
      [&route_level_config](
          envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto default_route =
            hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
        default_route->mutable_route()->set_cluster("cluster_0");
        default_route->mutable_match()->set_prefix("/path1");
        envoy::extensions::filters::http::aws_request_signing::v3::AwsRequestSigningPerRoute
            per_route_config;

        TestUtility::loadFromYaml(fmt::format(fmt::runtime(route_level_config), "ap-southeast-1"),
                                  per_route_config);
        auto config = default_route->mutable_typed_per_filter_config();
        (*config)["envoy.filters.http.aws_request_signing"].PackFrom(per_route_config);
        // (*config)["envoy.filters.http.aws_request_signing"].PackFrom(fmt::format(fmt::runtime(route_level_config),
        // "us-east-1")); Add route that should direct to cluster with custom bind config.
        auto next_route =
            hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes()->Add();
        next_route->mutable_route()->set_cluster("cluster_0");
        next_route->mutable_match()->set_prefix("/path2");
        TestUtility::loadFromYaml(fmt::format(fmt::runtime(route_level_config), "ap-southeast-2"),
                                  per_route_config);

        config = hcm.mutable_route_config()
                     ->mutable_virtual_hosts(0)
                     ->mutable_routes(1)
                     ->mutable_typed_per_filter_config();
        (*config)["envoy.filters.http.aws_request_signing"].PackFrom(per_route_config);
        next_route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes()->Add();
        next_route->mutable_route()->set_cluster("cluster_0");
        next_route->mutable_match()->set_prefix("/path3");
        TestUtility::loadFromYaml(fmt::format(fmt::runtime(route_level_config), "eu-west-1"),
                                  per_route_config);

        config = hcm.mutable_route_config()
                     ->mutable_virtual_hosts(0)
                     ->mutable_routes(2)
                     ->mutable_typed_per_filter_config();
        (*config)["envoy.filters.http.aws_request_signing"].PackFrom(per_route_config);
        next_route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes()->Add();
        next_route->mutable_route()->set_cluster("cluster_0");
        next_route->mutable_match()->set_prefix("/path4");
        TestUtility::loadFromYaml(fmt::format(fmt::runtime(route_level_config), "eu-west-2"),
                                  per_route_config);

        config = hcm.mutable_route_config()
                     ->mutable_virtual_hosts(0)
                     ->mutable_routes(3)
                     ->mutable_typed_per_filter_config();
        (*config)["envoy.filters.http.aws_request_signing"].PackFrom(per_route_config);
        next_route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes()->Add();
        next_route->mutable_route()->set_cluster("cluster_0");
        next_route->mutable_match()->set_prefix("/path5");
        TestUtility::loadFromYaml(fmt::format(fmt::runtime(route_level_config), "eu-west-3"),
                                  per_route_config);

        config = hcm.mutable_route_config()
                     ->mutable_virtual_hosts(0)
                     ->mutable_routes(4)
                     ->mutable_typed_per_filter_config();
        (*config)["envoy.filters.http.aws_request_signing"].PackFrom(per_route_config);
      });

  initialize();

  test_server_->waitForCounterGe("aws.metadata_credentials_provider.sts_token_service_internal-ap-"
                                 "southeast-1.credential_refreshes_performed",
                                 1, std::chrono::seconds(10));
  test_server_->waitForCounterGe("aws.metadata_credentials_provider.sts_token_service_internal-ap-"
                                 "southeast-2.credential_refreshes_performed",
                                 1, std::chrono::seconds(10));
  test_server_->waitForCounterGe("aws.metadata_credentials_provider.sts_token_service_internal-eu-"
                                 "west-1.credential_refreshes_performed",
                                 1, std::chrono::seconds(10));
  test_server_->waitForCounterGe("aws.metadata_credentials_provider.sts_token_service_internal-eu-"
                                 "west-2.credential_refreshes_performed",
                                 1, std::chrono::seconds(10));
  test_server_->waitForCounterGe("aws.metadata_credentials_provider.sts_token_service_internal-eu-"
                                 "west-3.credential_refreshes_performed",
                                 1, std::chrono::seconds(10));
}

TEST_F(InitializeFilterTest, TestWithTwoClustersRouteLevelAndStandard) {
  dnsSetup();
  // Web Identity Credentials and Container Credentials
  TestEnvironment::setEnvVar("AWS_EC2_METADATA_DISABLED", "true", 1);
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);
  TestEnvironment::setEnvVar("AWS_CONTAINER_CREDENTIALS_RELATIVE_URI", "/path/to/creds", 1);
  TestEnvironment::setEnvVar("AWS_CONTAINER_AUTHORIZATION_TOKEN", "auth_token", 1);
  addStandardFilter();
  addPerRouteFilter(AWS_REQUEST_SIGNING_CONFIG_SIGV4_ROUTE_LEVEL);
  initialize();
  test_server_->waitForCounterGe("aws.metadata_credentials_provider.ecs_task_"
                                 "metadata_server_internal.credential_refreshes_performed",
                                 1);

  test_server_->waitForCounterGe("aws.metadata_credentials_provider.sts_token_service_internal-ap-"
                                 "southeast-2.credential_refreshes_performed",
                                 1, std::chrono::seconds(10));
}

TEST_F(InitializeFilterTest, TestWithTwoClustersStandardInstanceProfile) {
  dnsSetup();
  // Web Identity Credentials, Container Credentials and Instance Profile Credentials
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);
  addStandardFilter();
  initialize();
  test_server_->waitForCounterGe("aws.metadata_credentials_provider.ec2_instance_"
                                 "metadata_server_internal.credential_refreshes_performed",
                                 1);

  test_server_->waitForCounterGe("aws.metadata_credentials_provider.sts_token_service_internal-ap-"
                                 "southeast-2.credential_refreshes_performed",
                                 1, std::chrono::seconds(10));
}

TEST_F(InitializeFilterTest, TestWithTwoClustersRouteLevelInstanceProfile) {
  dnsSetup();
  // Web Identity Credentials, Container Credentials and Instance Profile Credentials
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);
  addPerRouteFilter(AWS_REQUEST_SIGNING_CONFIG_SIGV4_ROUTE_LEVEL);
  initialize();
  test_server_->waitForCounterGe("aws.metadata_credentials_provider.ec2_instance_"
                                 "metadata_server_internal.credential_refreshes_performed",
                                 1);

  test_server_->waitForCounterGe("aws.metadata_credentials_provider.sts_token_service_internal-ap-"
                                 "southeast-2.credential_refreshes_performed",
                                 1, std::chrono::seconds(10));
}

TEST_F(InitializeFilterTest, TestWithTwoClustersRouteLevelAndStandardInstanceProfile) {
  dnsSetup();
  // Web Identity Credentials, Container Credentials and Instance Profile Credentials
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);
  addStandardFilter();
  addPerRouteFilter(AWS_REQUEST_SIGNING_CONFIG_SIGV4_ROUTE_LEVEL);
  initialize();
  test_server_->waitForCounterGe("aws.metadata_credentials_provider.ec2_instance_"
                                 "metadata_server_internal.credential_refreshes_performed",
                                 1);

  test_server_->waitForCounterGe("aws.metadata_credentials_provider.sts_token_service_internal-ap-"
                                 "southeast-2.credential_refreshes_performed",
                                 1, std::chrono::seconds(10));
}

class CdsInteractionTest : public testing::Test, public HttpIntegrationTest {
public:
  CdsInteractionTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, Network::Address::IpVersion::v4),
        dns_cluster_factory_(logical_dns_cluster_factory_),
        registered_dns_factory_(dns_resolver_factory_) {}

  void addStandardFilter(bool downstream = true) {
    config_helper_.prependFilter(AWS_REQUEST_SIGNING_CONFIG_SIGV4, downstream);
  }

  void expectResolve(Network::DnsLookupFamily, const std::string& expected_address) {
    EXPECT_CALL(*dns_resolver_, resolve(expected_address, _, _))
        .WillRepeatedly(Invoke([&](const std::string&, Network::DnsLookupFamily,
                                   Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
          cb(Network::DnsResolver::ResolutionStatus::Completed, "",
             TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));

          return nullptr;
        }));
  }

  void dnsSetup() {
    ON_CALL(dns_resolver_factory_, createDnsResolver(_, _, _)).WillByDefault(Return(dns_resolver_));
    expectResolve(Network::DnsLookupFamily::V4Only, "sts.ap-southeast-2.amazonaws.com");
  }

  NiceMock<MockLogicalDnsClusterFactory> logical_dns_cluster_factory_;
  Registry::InjectFactory<Envoy::Upstream::LogicalDnsClusterFactory> dns_cluster_factory_;
  NiceMock<Network::MockDnsResolverFactory> dns_resolver_factory_;
  Registry::InjectFactory<Network::DnsResolverFactory> registered_dns_factory_;

  Network::DnsResolver::ResolveCb dns_callback_;
  Network::MockActiveDnsQuery active_dns_query_;
  std::shared_ptr<NiceMock<Network::MockDnsResolver>> dns_resolver_{
      new NiceMock<Network::MockDnsResolver>};
  NiceMock<Event::MockDispatcher> dispatcher_;

  void SetUp() override {
    TestEnvironment::unsetEnvVar("AWS_ACCESS_KEY_ID");
    TestEnvironment::unsetEnvVar("AWS_SECRET_ACCESS_KEY");
    TestEnvironment::unsetEnvVar("AWS_SESSION_TOKEN");
    TestEnvironment::unsetEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE");
    TestEnvironment::unsetEnvVar("AWS_ROLE_ARN");
    TestEnvironment::unsetEnvVar("AWS_ROLE_SESSION_NAME");
    TestEnvironment::unsetEnvVar("AWS_CONTAINER_CREDENTIALS_FULL_URI");
    TestEnvironment::unsetEnvVar("AWS_CONTAINER_AUTHORIZATION_TOKEN");
  }
};

TEST_F(CdsInteractionTest, CDSUpdateDoesNotRemoveOurClusters) {

  // STS cluster requires dns mocking
  dnsSetup();

  // Web Identity Credentials only
  TestEnvironment::setEnvVar("AWS_EC2_METADATA_DISABLED", "true", 1);
  TestEnvironment::setEnvVar("AWS_WEB_IDENTITY_TOKEN_FILE", "/path/to/web_token", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_ARN", "aws:iam::123456789012:role/arn", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);

  CdsHelper cds_helper_;

  // Add CDS cluster using cds helper
  config_helper_.addConfigModifier(
      [&cds_helper_](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
        bootstrap.mutable_dynamic_resources()->mutable_cds_config()->set_resource_api_version(
            envoy::config::core::v3::ApiVersion::V3);
        bootstrap.mutable_dynamic_resources()
            ->mutable_cds_config()
            ->mutable_path_config_source()
            ->set_path(cds_helper_.cdsPath());
        bootstrap.mutable_static_resources()->clear_clusters();
      });

  // Don't validate clusters so we can use the CDS cluster as a route target
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.mutable_route_config()->mutable_validate_clusters()->set_value(false); });

  addStandardFilter();

  // Use CDS helper to add initial CDS cluster
  envoy::config::cluster::v3::Cluster cluster_;
  cluster_.mutable_connect_timeout()->CopyFrom(
      Protobuf::util::TimeUtil::MillisecondsToDuration(100));
  cluster_.set_name("cluster_0");
  cluster_.set_lb_policy(envoy::config::cluster::v3::Cluster::ROUND_ROBIN);

  cds_helper_.setCds({cluster_});

  initialize();
  test_server_->waitForCounterGe("cluster_manager.cluster_added", 2);

  cluster_.set_name("testing");
  cds_helper_.setCds({cluster_});

  test_server_->waitForCounterGe("cluster_manager.cds.update_success", 2);
  EXPECT_EQ(1, test_server_->counter("cluster_manager.cluster_removed")->value());
  EXPECT_EQ(3, test_server_->counter("cluster_manager.cluster_added")->value());
}

} // namespace
} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
