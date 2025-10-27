#include "envoy/extensions/filters/http/aws_request_signing/v3/aws_request_signing.pb.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/extensions/clusters/dns/dns_cluster.h"
#include "source/extensions/network/dns_resolver/getaddrinfo/getaddrinfo.h"

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

const std::string AWS_REQUEST_SIGNING_CONFIG_SIGV4_ROLES_ANYWHERE = R"EOF(
aws_request_signing:
  credential_provider:
    iam_roles_anywhere_credential_provider:
      role_arn: arn:aws:iam::012345678901:role/rolesanywhere
      certificate: {environment_variable: CERT}
      private_key: {environment_variable: PKEY}
      trust_anchor_arn: arn:aws:rolesanywhere:ap-southeast-2:012345678901:trust-anchor/8d105284-f0a7-4939-a7e6-8df768ea535f
      profile_arn: arn:aws:rolesanywhere:ap-southeast-2:012345678901:profile/4af0c6cf-506a-4469-b1b5-5f3fecdaabdf
      session_duration: 900s
  service_name: vpc-lattice-svcs
  region: ap-southeast-2
  signing_algorithm: aws_sigv4
  use_unsigned_payload: true
  match_excluded_headers:
  - prefix: x-envoy
  - prefix: x-forwarded
  - exact: x-amzn-trace-id
stat_prefix: some-prefix
  )EOF";

const std::string AWS_REQUEST_SIGNING_CONFIG_SIGV4_ROLES_ANYWHERE_CUSTOM = R"EOF(
aws_request_signing:
  credential_provider:
    custom_credential_provider_chain: true
    iam_roles_anywhere_credential_provider:
      role_arn: arn:aws:iam::012345678901:role/rolesanywhere
      certificate: {environment_variable: CERT}
      private_key: {environment_variable: PKEY}
      trust_anchor_arn: arn:aws:rolesanywhere:ap-southeast-2:012345678901:trust-anchor/8d105284-f0a7-4939-a7e6-8df768ea535f
      profile_arn: arn:aws:rolesanywhere:ap-southeast-2:012345678901:profile/4af0c6cf-506a-4469-b1b5-5f3fecdaabdf
      session_duration: 900s
  service_name: vpc-lattice-svcs
  region: ap-southeast-2
  signing_algorithm: aws_sigv4
  use_unsigned_payload: true
  match_excluded_headers:
  - prefix: x-envoy
  - prefix: x-forwarded
  - exact: x-amzn-trace-id
stat_prefix: some-prefix
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

// Example certificates generated for test cases - 100 year expiration
std::string server_root_cert_rsa_pem = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFZzCCA0+gAwIBAgIUUDBf/hk/8LUQstasvM05ipfkQcQwDQYJKoZIhvcNAQEL
BQAwQjELMAkGA1UEBhMCWFgxFTATBgNVBAcMDERlZmF1bHQgQ2l0eTEcMBoGA1UE
CgwTRGVmYXVsdCBDb21wYW55IEx0ZDAgFw0yNTA1MTcwMjI1MDlaGA8yMTI1MDQy
MzAyMjUwOVowQjELMAkGA1UEBhMCWFgxFTATBgNVBAcMDERlZmF1bHQgQ2l0eTEc
MBoGA1UECgwTRGVmYXVsdCBDb21wYW55IEx0ZDCCAiIwDQYJKoZIhvcNAQEBBQAD
ggIPADCCAgoCggIBAN2mXm3KC0yiIMtGJsQ9MWS7CPeFbvVhhAlfLKkHpTKWL6dD
nQ8xpIUPyKzOqqWGj+pkoVUDnbXEjBVFESdlGo4M50ubJ+d9/WOb9IeHQTZWjTyK
8VzRdA057y13NtDjclmu4ScjGxYRfLiN0oZl/YxAzFUK8FeYejpi/aWSwfLU2CnK
0YgArgm0P6tDDQja2Mj/H84xzG/CKuAIZmG5TNikgemuwlPhkx0uLEoP0zumeiDq
HQa78qRtyJlh84pDNqEH+UCxuKR/Mcy4WmLd4Ra/TqANvCUMhbJubHgibHZa4ZLz
YaiMbPV3CAFZtmZY82oCDBLqxWyX2O6LfJXQKZHKgRrbWgzafPhdQ1tjHXBoMFE0
Mwi0APGoqJlFbfUqAbCiWXYNPfmnsR49ctefnQ23hZaF8MdUI8Cz24ua+UFNLh2f
JCQzb2yLOCNEMjY8L6VnCNGUyfcB7uqkpFNldBLj17xErDXBlNS2ARpouECK/VGQ
LpDdl7ZiAEKAYX52TVCDmqAnc4eIAXVIDjB19NdwS0LwSLn4g+A0kPtsxhWHEvLv
/M4aZPDLfYWgiG+Y5kD2fELsh5EDwIA0/CIpJz2GujgvUqPigOUqZ/1Uh/Tk+Tl/
k6xmCiENnHM9YtsnDsxgkGW9nnF+As8v3I0zuixOtuA5B3XkPHDLU2KcmbZLAgMB
AAGjUzBRMB0GA1UdDgQWBBQipLm+Isi5gDvOQXYKYVErC2OHVzAfBgNVHSMEGDAW
gBQipLm+Isi5gDvOQXYKYVErC2OHVzAPBgNVHRMBAf8EBTADAQH/MA0GCSqGSIb3
DQEBCwUAA4ICAQDTAUdAYdjvZaUfMxWHdd7tLYJpnB9bOa2DTEcWRE6nPa6pMnAe
ilW/6INJyu2mx+AkJgj9LRkt2NrhbKyFsuRI4xXv2aUBesdmBep1VNGvaTtpEHLk
zCl+44pdC/VOJy4yKnwgScno+WrW1UwngNBK7ZGFqwv2zQxiz5YJo4btraPjvO4o
orExqCKN6/Z0pE7rxDqlp9dZCKOPhFXff7EqEfCjZ2pL6wxS9EritlNW8q1v/nPV
YxSOtx4A1/ps48yo6LGyHlOOpNyIAXt11Ert0YZM29YBrIMl0mhVtdnKpio3fqh5
/qdJo9qa3ohkBG2ks2FdHirJh6aSprwjus4dJvOgzmx4o7cfLIWQgKMtdCACHw2T
14BFDYYZMMAoahCi3obW7NKmH7edp1Fig4CRnwjBMkBld7XmL4X1x5fXodRyUG7V
1h5zUVuYHSPqUKNXNgyKwzfAXGd5hVDlyxUp46itdZ3zk6RCROZooa4y3Znoe4d0
vAMHGhD/7ZqV9bc0ZfItIHlmERrhOKaDsdwAbf14PSWt2bD0fTVa8erElgGB9kgL
IID8F7S+eEeSBcKQU5ayDMYp61s5XoDLMQ4HnozelK6Jx1q8iVf8TuqbWtnIIImP
2Rk1s46j2pk8H1NjFZBi4FgC3rvCFf8opPrCDyKCEysvr3u/8ZXGCThn8g==
-----END CERTIFICATE-----
)EOF";

std::string server_root_private_key_rsa_pem = R"EOF(
-----BEGIN PRIVATE KEY-----
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
ZW8j9fw47vqG9oCPqG2CHOIlN+IxBGM=
-----END PRIVATE KEY-----
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

    addBootstrapDefaultDns();
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

  void addBootstrapDefaultDns() {
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* typed_dns_resolver_config = bootstrap.mutable_typed_dns_resolver_config();
      typed_dns_resolver_config->set_name("envoy.network.dns_resolver.getaddrinfo");
      envoy::extensions::network::dns_resolver::getaddrinfo::v3::GetAddrInfoDnsResolverConfig
          config;
      typed_dns_resolver_config->mutable_typed_config()->PackFrom(config);
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
class MockLogicalDnsClusterFactory : public Upstream::DnsClusterFactory {
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
  Registry::InjectFactory<Envoy::Upstream::DnsClusterFactory> dns_cluster_factory_;
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

  void dnsSetup(std::string hostname) {
    ON_CALL(dns_resolver_factory_, createDnsResolver(_, _, _)).WillByDefault(Return(dns_resolver_));
    expectResolve(Network::DnsLookupFamily::V4Only, hostname);
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
  dnsSetup("sts.ap-southeast-2.amazonaws.com");

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
  dnsSetup("sts.ap-southeast-2.amazonaws.com");

  TestEnvironment::setEnvVar("AWS_EC2_METADATA_DISABLED", "true", 1);
  addCustomCredentialChainFilter();
  initialize();

  test_server_->waitForCounterGe("aws.metadata_credentials_provider.sts_token_service_internal-ap-"
                                 "southeast-2.credential_refreshes_performed",
                                 1, std::chrono::seconds(10));
}

TEST_F(InitializeFilterTest, TestWithOneClusterStandardUpstream) {

  // Web Identity Credentials only
  dnsSetup("sts.ap-southeast-2.amazonaws.com");

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
  dnsSetup("sts.ap-southeast-2.amazonaws.com");

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
  dnsSetup("sts.ap-southeast-2.amazonaws.com");
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
  dnsSetup("sts.ap-southeast-2.amazonaws.com");
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
  dnsSetup("sts.ap-southeast-2.amazonaws.com");
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
  dnsSetup("sts.ap-southeast-2.amazonaws.com");
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

TEST_F(InitializeFilterTest, TestWithIAMRolesAnywhereCluster) {
  dnsSetup("rolesanywhere.ap-southeast-2.amazonaws.com");
  // RolesAnywhere credentials only
  TestEnvironment::setEnvVar("AWS_EC2_METADATA_DISABLED", "true", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);
  auto cert_env = std::string("CERT");
  TestEnvironment::setEnvVar(cert_env, server_root_cert_rsa_pem, 1);
  auto pkey_env = std::string("PKEY");
  TestEnvironment::setEnvVar(pkey_env, server_root_private_key_rsa_pem, 1);

  addPerRouteFilter(AWS_REQUEST_SIGNING_CONFIG_SIGV4_ROLES_ANYWHERE);
  initialize();
  test_server_->waitForCounterGe("aws.metadata_credentials_provider.rolesanywhere_ap-southeast-2_"
                                 "amazonaws_com.credential_refreshes_performed",
                                 1);
}

TEST_F(InitializeFilterTest, TestWithIAMRolesAnywhereCustom) {
  dnsSetup("rolesanywhere.ap-southeast-2.amazonaws.com");
  // RolesAnywhere credentials only
  TestEnvironment::setEnvVar("AWS_EC2_METADATA_DISABLED", "true", 1);
  TestEnvironment::setEnvVar("AWS_ROLE_SESSION_NAME", "role-session-name", 1);
  auto cert_env = std::string("CERT");
  TestEnvironment::setEnvVar(cert_env, server_root_cert_rsa_pem, 1);
  auto pkey_env = std::string("PKEY");
  TestEnvironment::setEnvVar(pkey_env, server_root_private_key_rsa_pem, 1);
  // Set system time for these tests to ensure certs do not expire

  addPerRouteFilter(AWS_REQUEST_SIGNING_CONFIG_SIGV4_ROLES_ANYWHERE_CUSTOM);
  initialize();
  test_server_->waitForCounterGe("aws.metadata_credentials_provider.rolesanywhere_ap-southeast-2_"
                                 "amazonaws_com.credential_refreshes_performed",
                                 1);
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
  dnsSetup("sts.ap-southeast-2.amazonaws.com");
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
  dnsSetup("sts.ap-southeast-2.amazonaws.com");
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
  dnsSetup("sts.ap-southeast-2.amazonaws.com");
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
  dnsSetup("sts.ap-southeast-2.amazonaws.com");
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

  void dnsSetup(std::string hostname) {
    ON_CALL(dns_resolver_factory_, createDnsResolver(_, _, _)).WillByDefault(Return(dns_resolver_));
    expectResolve(Network::DnsLookupFamily::V4Only, hostname);
  }

  NiceMock<MockLogicalDnsClusterFactory> logical_dns_cluster_factory_;
  Registry::InjectFactory<Envoy::Upstream::DnsClusterFactory> dns_cluster_factory_;
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
  dnsSetup("sts.ap-southeast-2.amazonaws.com");

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
