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
    auto cfg = std::move(*cfg_or_error);
    static auto* upstream_stats_store = new Stats::TestIsolatedStoreImpl();
    auto factory_or_error = Extensions::TransportSockets::Tls::ServerSslSocketFactory::create(
        std::move(cfg), BaseIntegrationTest::context_manager_, *upstream_stats_store->rootScope());
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

} // namespace Ssl
} // namespace Envoy
