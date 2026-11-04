#include "source/common/tls/server_context_config_impl.h"
#include "source/common/tls/server_ssl_socket.h"

#include "test/integration/http_integration.h"

namespace Envoy {
namespace Ssl {

class SslUpstreamIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                   public HttpIntegrationTest {
public:
  SslUpstreamIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {}

  void initialize() override {
    upstream_tls_ = true;
    config_helper_.configureUpstreamTls(
        true /* use alpn */, false /* http3 */, std::nullopt,
        [this](envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext& ctx) {
          ctx.set_auto_host_sni(auto_host_sni_);
          ctx.set_auto_sni_san_validation(auto_sni_san_validation_);

          ctx.set_sni(default_sni_);
        });

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      cluster->mutable_load_assignment()
          ->mutable_endpoints(0)
          ->mutable_lb_endpoints(0)
          ->mutable_endpoint()
          ->set_hostname(configured_upstream_hostname_);

      auto protocol_options =
          MessageUtil::anyConvert<envoy::extensions::upstreams::http::v3::HttpProtocolOptions>(
              (*cluster->mutable_typed_extension_protocol_options())
                  ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]);
      auto* upstream_http_options = protocol_options.mutable_upstream_http_protocol_options();

      upstream_http_options->set_auto_sni(auto_sni_);
      upstream_http_options->set_auto_san_validation(auto_san_validation_);
      std::ignore = (*cluster->mutable_typed_extension_protocol_options())
                        ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
                            .PackFrom(protocol_options);
    });

    HttpIntegrationTest::initialize();
  }

  std::string testSni() {
    initialize();

    codec_client_ = makeHttpConnection((lookupPort("http")));
    default_request_headers_.setHost(sni_header_);
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

    EXPECT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    EXPECT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    std::string sni = fake_upstream_connection_->connection().ssl()->sni();
    upstream_request_->encodeHeaders(default_response_headers_, true);
    EXPECT_TRUE(fake_upstream_connection_->close(Network::ConnectionCloseType::FlushWrite));
    EXPECT_TRUE(response->waitForEndStream());
    codec_client_->close();

    return sni;
  }

  void expectCertValidationFailure() {
    autonomous_upstream_ = true;
    initialize();

    codec_client_ = makeHttpConnection((lookupPort("http")));
    default_request_headers_.setHost(sni_header_);
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    EXPECT_TRUE(response->waitForEndStream());
    EXPECT_EQ("503", response->headers().getStatusValue());
    codec_client_->close();

    test_server_->waitForCounter("cluster.cluster_0.ssl.fail_verify_san", testing::Eq(1));
  }

  void expectCertValidationSuccess() { testSni(); }

  static constexpr absl::string_view upstream_hostname_ = "upstream_name.example.com";
  static constexpr absl::string_view default_sni_ = "default_sni.example.com";
  static constexpr absl::string_view default_sni_header_ = "sni_header.example.com";

  bool auto_sni_{false};                // Get SNI from downstream :authority header.
  bool auto_san_validation_{false};     // Validate SAN against value of :authority header.
  bool auto_host_sni_{false};           // Get SNI from cluster host config.
  bool auto_sni_san_validation_{false}; // Validate SAN against transmitted SNI value.

  std::string configured_upstream_hostname_{upstream_hostname_};
  std::string sni_header_{default_sni_header_};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SslUpstreamIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(SslUpstreamIntegrationTest, DefaultSni) { EXPECT_EQ(default_sni_, testSni()); }

TEST_P(SslUpstreamIntegrationTest, AutoSni) {
  auto_sni_ = true;
  EXPECT_EQ(sni_header_, testSni());
}

TEST_P(SslUpstreamIntegrationTest, AutoHostSni) {
  auto_host_sni_ = true;
  EXPECT_EQ(upstream_hostname_, testSni());
}

// When both `auto_sni` and `auto_host_sni` are configured, `auto_sni` takes precedence.
TEST_P(SslUpstreamIntegrationTest, SniHeaderOverridesHost) {
  auto_sni_ = true;
  auto_host_sni_ = true;
  EXPECT_EQ(sni_header_, testSni());
}

// When `auto_host_sni` is configured but the upstream host does not have a hostname, fallback
// to the default SNI.
TEST_P(SslUpstreamIntegrationTest, AutoHostSniMissingConfig) {
  auto_host_sni_ = true;
  configured_upstream_hostname_.clear();
  EXPECT_EQ(default_sni_, testSni());
}

TEST_P(SslUpstreamIntegrationTest, AutoSniSanMatchFail) {
  auto_host_sni_ = true;
  auto_sni_san_validation_ = true;
  expectCertValidationFailure();
}

TEST_P(SslUpstreamIntegrationTest, AutoSniSanMatchSuccess) {
  // Upstream cert has SAN `*.lyft.com`.
  configured_upstream_hostname_ = "test.lyft.com";

  auto_host_sni_ = true;
  auto_sni_san_validation_ = true;
  expectCertValidationSuccess();
}

// When both `auto_sni_san_validation` and and `auto_san_validation` are set, the latter takes
// precedence.
TEST_P(SslUpstreamIntegrationTest, BothAutoValidationPreferenceFailure) {
  // Upstream cert has SAN `*.lyft.com`.
  configured_upstream_hostname_ = "test.lyft.com";
  sni_header_ = "test.example.com";

  auto_host_sni_ = true;
  auto_sni_san_validation_ = true;
  auto_san_validation_ = true;
  expectCertValidationFailure();
}

// When both `auto_sni_san_validation` and and `auto_san_validation` are set, the latter takes
// precedence.
TEST_P(SslUpstreamIntegrationTest, BothAutoValidationPreferenceSuccess) {
  // Upstream cert has SAN `*.lyft.com`.
  configured_upstream_hostname_ = "test.example.com";
  sni_header_ = "test.lyft.com";

  auto_host_sni_ = true;
  auto_sni_san_validation_ = true;
  auto_san_validation_ = true;
  expectCertValidationSuccess();
}

class SslUpstreamNulSanIntegrationTest : public SslUpstreamIntegrationTest {
public:
  Network::DownstreamTransportSocketFactoryPtr
  createCustomUpstreamTlsContext(const FakeUpstreamConfig& /*upstream_config*/) {
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
    const std::string rundir = TestEnvironment::runfilesDirectory();

    tls_context.mutable_common_tls_context()
        ->mutable_validation_context()
        ->mutable_trusted_ca()
        ->set_filename(rundir + "/test/config/integration/certs/upstreamcacert.pem");

    auto* certs = tls_context.mutable_common_tls_context()->add_tls_certificates();
    certs->mutable_certificate_chain()->set_filename(
        rundir + "/test/config/integration/certs/san_nul_servercert.pem");
    certs->mutable_private_key()->set_filename(rundir +
                                               "/test/config/integration/certs/upstreamkey.pem");

    tls_context.mutable_common_tls_context()->add_alpn_protocols("http/1.1");

    auto cfg_or_error = Extensions::TransportSockets::Tls::ServerContextConfigImpl::create(
        tls_context, factory_context_, {}, false);
    RELEASE_ASSERT(cfg_or_error.ok(), std::string(cfg_or_error.status().message()));
    auto factory_or_error = Extensions::TransportSockets::Tls::ServerSslSocketFactory::create(
        std::move(*cfg_or_error), BaseIntegrationTest::context_manager_,
        server_factory_context_.serverScope());
    RELEASE_ASSERT(factory_or_error.ok(), std::string(factory_or_error.status().message()));
    return std::move(*factory_or_error);
  }

  void createUpstreams() override {
    for (uint32_t i = 0; i < fake_upstreams_count_; ++i) {
      auto endpoint = upstream_address_fn_(i);
      FakeUpstreamConfig config = upstreamConfig();
      Network::DownstreamTransportSocketFactoryPtr factory = createCustomUpstreamTlsContext(config);
      fake_upstreams_.emplace_back(std::make_unique<AutonomousUpstream>(
          std::move(factory), endpoint, config, autonomous_allow_incomplete_streams_));
    }
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SslUpstreamNulSanIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(SslUpstreamNulSanIntegrationTest, EmbeddedNulSanValidation) {
  auto_sni_ = true;
  auto_san_validation_ = true;
  sni_header_ = "example.com";
  expectCertValidationFailure();
}

// The upstream rejects the client certificate that Envoy presents. With TLS 1.3, Envoy completes
// its handshake before the upstream validates the client certificate, so the rejection is reported
// as a reset after the connection is established rather than as a connection failure.
class SslUpstreamClientCertRejectedIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  SslUpstreamClientCertRejectedIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    setUpstreamProtocol(Http::CodecType::HTTP1);
  }

  void initialize() override {
    config_helper_.configureUpstreamTls(
        false /* use alpn */, false /* http3 */, std::nullopt,
        [](envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext& ctx) {
          auto* tls_cert = ctx.mutable_common_tls_context()->add_tls_certificates();
          tls_cert->mutable_certificate_chain()->set_filename(
              TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem"));
          tls_cert->mutable_private_key()->set_filename(
              TestEnvironment::runfilesPath("test/config/integration/certs/clientkey.pem"));
        });
    HttpIntegrationTest::initialize();
  }

  // The upstream requires a client certificate, but trusts a CA that didn't sign Envoy's.
  Network::DownstreamTransportSocketFactoryPtr createCustomUpstreamTlsContext() {
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
    tls_context.mutable_require_client_certificate()->set_value(true);
    auto* common_tls_context = tls_context.mutable_common_tls_context();
    auto* tls_cert = common_tls_context->add_tls_certificates();
    tls_cert->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcert.pem"));
    tls_cert->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamkey.pem"));
    common_tls_context->mutable_validation_context()->mutable_trusted_ca()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));

    auto cfg_or_error = Extensions::TransportSockets::Tls::ServerContextConfigImpl::create(
        tls_context, factory_context_, {}, false);
    RELEASE_ASSERT(cfg_or_error.ok(), std::string(cfg_or_error.status().message()));
    auto factory_or_error = Extensions::TransportSockets::Tls::ServerSslSocketFactory::create(
        std::move(*cfg_or_error), BaseIntegrationTest::context_manager_,
        server_factory_context_.serverScope());
    RELEASE_ASSERT(factory_or_error.ok(), std::string(factory_or_error.status().message()));
    return std::move(*factory_or_error);
  }

  void createUpstreams() override {
    for (uint32_t i = 0; i < fake_upstreams_count_; ++i) {
      fake_upstreams_.emplace_back(std::make_unique<AutonomousUpstream>(
          createCustomUpstreamTlsContext(), upstream_address_fn_(i), upstreamConfig(),
          autonomous_allow_incomplete_streams_));
    }
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SslUpstreamClientCertRejectedIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// A client certificate rejected with TLS 1.3 is retried by the `reset` retry policy, but isn't
// counted as a connection failure.
TEST_P(SslUpstreamClientCertRejectedIntegrationTest, RetryOnReset) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* retry_policy = hcm.mutable_route_config()
                                 ->mutable_virtual_hosts(0)
                                 ->mutable_routes(0)
                                 ->mutable_route()
                                 ->mutable_retry_policy();
        retry_policy->set_retry_on("reset");
        retry_policy->mutable_num_retries()->set_value(1);
      });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  codec_client_->close();

  // Both the request and its retry negotiated TLS 1.3 and were rejected after the handshake.
  test_server_->waitForCounter("cluster.cluster_0.ssl.versions.TLSv1.3", testing::Eq(2));
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_rq_retry")->value());
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_cx_connect_fail")->value());
}

} // namespace Ssl
} // namespace Envoy
