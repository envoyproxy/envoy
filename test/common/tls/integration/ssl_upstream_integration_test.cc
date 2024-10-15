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
        true /* use alpn */, false /* http3 */, absl::nullopt,
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

      (*cluster->mutable_typed_extension_protocol_options())
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
    initialize();

    codec_client_ = makeHttpConnection((lookupPort("http")));
    default_request_headers_.setHost(sni_header_);
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

    EXPECT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    std::string sni = fake_upstream_connection_->connection().ssl()->sni();
    EXPECT_TRUE(fake_upstream_connection_->close(Network::ConnectionCloseType::FlushWrite));
    EXPECT_TRUE(response->waitForEndStream());
    EXPECT_EQ("503", response->headers().getStatusValue());
    codec_client_->close();

    test_server_->waitForCounterEq("cluster.cluster_0.ssl.fail_verify_san", 1);
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

} // namespace Ssl
} // namespace Envoy
