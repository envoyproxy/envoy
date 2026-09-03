#include <openssl/ssl.h>

#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "source/common/tls/connection_info_impl_base.h"

#include "test/integration/http_protocol_integration.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Tests that upstream QUIC connections present the configured client certificate and that
// the fake upstream properly verifies it using server-side mTLS support over QUIC.
class QuicUpstreamClientCertIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override {
    upstream_tls_ = true;

    // Configure the cluster with a client certificate.
    config_helper_.configureUpstreamTls(
        /*use_alpn=*/false, /*http3=*/true, /*alternate_protocol_cache_config=*/{},
        [this](envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext& tls_context) {
          if (configure_client_cert_) {
            auto* certs = tls_context.mutable_common_tls_context()->add_tls_certificates();
            certs->mutable_certificate_chain()->set_filename(
                TestEnvironment::runfilesPath(client_cert_path_));
            certs->mutable_private_key()->set_filename(
                TestEnvironment::runfilesPath(client_key_path_));
          }
        });
    HttpProtocolIntegrationTest::initialize();
  }

  void createUpstreams() override {
    for (uint32_t i = 0; i < fake_upstreams_count_; ++i) {
      auto endpoint = upstream_address_fn_(i);
      FakeUpstreamConfig config(upstreamConfig());
      Network::DownstreamTransportSocketFactoryPtr factory =
          upstream_tls_ ? createCustomUpstreamTlsContext(config)
                        : Network::Test::createRawBufferDownstreamSocketFactory();
      fake_upstreams_.emplace_back(
          std::make_unique<FakeUpstream>(std::move(factory), endpoint, config));
    }
  }

  Network::DownstreamTransportSocketFactoryPtr
  createCustomUpstreamTlsContext(const FakeUpstreamConfig& /*upstream_config*/) {
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
    const std::string rundir = TestEnvironment::runfilesDirectory();
    tls_context.mutable_common_tls_context()
        ->mutable_validation_context()
        ->mutable_trusted_ca()
        ->set_filename(rundir + "/test/config/integration/certs/cacert.pem");
    auto* certs = tls_context.mutable_common_tls_context()->add_tls_certificates();
    certs->mutable_certificate_chain()->set_filename(
        rundir + "/test/config/integration/certs/upstreamcert.pem");
    certs->mutable_private_key()->set_filename(rundir +
                                               "/test/config/integration/certs/upstreamkey.pem");
    if (require_client_cert_) {
      tls_context.mutable_require_client_certificate()->set_value(true);
    }
    envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport quic_config;
    quic_config.mutable_downstream_tls_context()->MergeFrom(tls_context);

    auto& config_factory = Config::Utility::getAndCheckFactoryByName<
        Server::Configuration::DownstreamTransportSocketConfigFactory>(
        "envoy.transport_sockets.quic");
    return *config_factory.createTransportSocketFactory(quic_config, factory_context_, {});
  }

  // Returns whether the fake upstream received a client certificate on the current upstream
  // connection. Runs on the fake upstream's connection thread for thread safety.
  bool upstreamSawClientCert() {
    bool cert_presented = false;
    absl::Notification done;
    Network::Connection& connection = fake_upstream_connection_->connection();
    fake_upstream_connection_->postToConnectionThread([&connection, &cert_presented, &done]() {
      auto ssl_info = std::dynamic_pointer_cast<
          const Extensions::TransportSockets::Tls::ConnectionInfoImplBase>(connection.ssl());
      ASSERT(ssl_info != nullptr);
      const STACK_OF(CRYPTO_BUFFER)* certs = SSL_get0_peer_certificates(ssl_info->ssl());
      cert_presented = certs != nullptr && sk_CRYPTO_BUFFER_num(certs) > 0;
      done.Notify();
    });
    done.WaitForNotification();
    return cert_presented;
  }

  void sendRequestAndExpectResponse() {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }

  bool configure_client_cert_{true};
  bool require_client_cert_{false};
  std::string client_cert_path_{"test/config/integration/certs/clientcert.pem"};
  std::string client_key_path_{"test/config/integration/certs/clientkey.pem"};
};

INSTANTIATE_TEST_SUITE_P(Protocols, QuicUpstreamClientCertIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1}, {Http::CodecType::HTTP3})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// The configured client certificate is presented and successfully validated on the upstream QUIC
// connection when the upstream requires one.
TEST_P(QuicUpstreamClientCertIntegrationTest, ClientCertificatePresentedAndValidated) {
  require_client_cert_ = true;
  initialize();
  sendRequestAndExpectResponse();
  EXPECT_TRUE(upstreamSawClientCert());
}

// With the runtime guard disabled, the client certificate is not installed on the QUIC client
// SSL context, so nothing is presented. When client cert is optional, handshake still succeeds.
TEST_P(QuicUpstreamClientCertIntegrationTest, NoClientCertificateWhenRuntimeDisabled) {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.quic_upstream_client_certificates",
                                    "false");
  initialize();
  sendRequestAndExpectResponse();
  EXPECT_FALSE(upstreamSawClientCert());
}

// When client cert is required by upstream and runtime guard is disabled, the upstream handshake
// fails and the request results in a 503.
TEST_P(QuicUpstreamClientCertIntegrationTest, RequireClientCertificateFailsWhenNoCertPresented) {
  require_client_cert_ = true;
  config_helper_.addRuntimeOverride("envoy.reloadable_features.quic_upstream_client_certificates",
                                    "false");
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
}

// When an untrusted client certificate is presented by upstream client, the fake upstream
// rejects it and the request results in a 503.
TEST_P(QuicUpstreamClientCertIntegrationTest, UntrustedClientCertificateRejected) {
  require_client_cert_ = true;
  // upstreamcert.pem is signed by upstreamcacert.pem, not cacert.pem.
  client_cert_path_ = "test/config/integration/certs/upstreamcert.pem";
  client_key_path_ = "test/config/integration/certs/upstreamkey.pem";
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
}

} // namespace
} // namespace Envoy
